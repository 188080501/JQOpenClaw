// .h include
#include "capabilities/system/systeminput.h"

// C++ lib import
#include <atomic>
#include <cmath>
#include <limits>

// Qt lib import
#include <QDebug>
#include <QHash>
#include <QJsonArray>
#include <QMutex>
#include <QMutexLocker>
#include <QRunnable>
#include <QThread>
#include <QThreadPool>
#include <QtGlobal>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace
{
const int inputMinActions = 1;
const int inputMaxActions = 1000;
const int inputDelayMinMs = 0;
const int inputDelayMaxMs = 60000;
const int inputTextIntervalMaxMs = 1000;
const int inputClickMinCount = 1;
const int inputClickMaxCount = 100;
const int inputScrollMinDelta = -12000;
const int inputScrollMaxDelta = 12000;
const int inputTapHoldMs = 40;
const int inputCancellationPollMs = 20;

enum class InputActionType
{
    MouseMove,
    MouseClick,
    MouseScroll,
    MouseDrag,
    KeyboardDown,
    KeyboardUp,
    KeyboardTap,
    KeyboardText,
    Delay
};

struct InputActionRequest
{
    InputActionType type = InputActionType::Delay;
    QString typeName;
    QString moveMode = QStringLiteral("absolute");
    int x = 0;
    int y = 0;
    QString mouseButton = QStringLiteral("left");
    int clickCount = 1;
    int scrollDeltaX = 0;
    int scrollDeltaY = 0;
    QString keyName;
    quint16 virtualKey = 0;
    QString text;
    int ms = 0;
    int intervalMs = 0;
};

const QString &inputCancelledReason()
{
    static const QString value = QStringLiteral("cancelled by newer request");
    return value;
}

std::atomic<quint64> &latestInputDispatchId()
{
    static std::atomic<quint64> value(0);
    return value;
}

QMutex &inputDispatchMutex()
{
    static QMutex value;
    return value;
}

quint64 markLatestInputDispatch()
{
    return latestInputDispatchId().fetch_add(1, std::memory_order_acq_rel) + 1;
}

bool isInputDispatchCurrent(quint64 dispatchId)
{
    return latestInputDispatchId().load(std::memory_order_acquire) == dispatchId;
}

bool setCancelledError(QString *error)
{
    if ( error != nullptr )
    {
        *error = inputCancelledReason();
    }
    return false;
}

bool executeAction(const InputActionRequest &action, quint64 dispatchId, QString *error);
#ifdef Q_OS_WIN
bool sendKeyboardVirtualKey(quint16 virtualKey, bool keyUp, QString *error);
#endif

QMutex &inputPressedKeysMutex()
{
    static QMutex value;
    return value;
}

QHash<quint64, QHash<quint16, int>> &pressedKeysByDispatch()
{
    static QHash<quint64, QHash<quint16, int>> value;
    return value;
}

void trackPressedKey(quint64 dispatchId, quint16 virtualKey)
{
    QMutexLocker locker(&inputPressedKeysMutex());
    QHash<quint16, int> &pressedKeys = pressedKeysByDispatch()[dispatchId];
    const int currentCount = pressedKeys.value(virtualKey, 0);
    pressedKeys.insert(virtualKey, currentCount + 1);
}

void untrackPressedKey(quint64 dispatchId, quint16 virtualKey)
{
    QMutexLocker locker(&inputPressedKeysMutex());
    auto dispatchIterator = pressedKeysByDispatch().find(dispatchId);
    if ( dispatchIterator == pressedKeysByDispatch().end() )
    {
        return;
    }

    QHash<quint16, int> &pressedKeys = dispatchIterator.value();
    const int currentCount = pressedKeys.value(virtualKey, 0);
    if ( currentCount <= 1 )
    {
        pressedKeys.remove(virtualKey);
    }
    else
    {
        pressedKeys.insert(virtualKey, currentCount - 1);
    }

    if ( pressedKeys.isEmpty() )
    {
        pressedKeysByDispatch().erase(dispatchIterator);
    }
}

QHash<quint16, int> takePressedKeys(quint64 dispatchId)
{
    QMutexLocker locker(&inputPressedKeysMutex());
    return pressedKeysByDispatch().take(dispatchId);
}

void releaseTrackedPressedKeys(quint64 dispatchId, const QString &reason)
{
    const QHash<quint16, int> pressedKeys = takePressedKeys(dispatchId);
    if ( pressedKeys.isEmpty() )
    {
        return;
    }

    int pendingReleaseCount = 0;
    for ( auto iterator = pressedKeys.constBegin(); iterator != pressedKeys.constEnd(); ++iterator )
    {
        pendingReleaseCount += iterator.value();
    }
    qWarning().noquote() << QStringLiteral(
        "[capability.system.input] releasing pressed keys requestId=%1 count=%2 reason=%3"
    ).arg(dispatchId).arg(pendingReleaseCount).arg(reason);

#ifdef Q_OS_WIN
    for ( auto iterator = pressedKeys.constBegin(); iterator != pressedKeys.constEnd(); ++iterator )
    {
        for ( int i = 0; i < iterator.value(); ++i )
        {
            QString releaseError;
            if ( !sendKeyboardVirtualKey(iterator.key(), true, &releaseError) )
            {
                qWarning().noquote() << QStringLiteral(
                    "[capability.system.input] failed to release key requestId=%1 key=%2 reason=%3"
                ).arg(dispatchId).arg(iterator.key()).arg(releaseError);
            }
        }
    }
#else
    Q_UNUSED(reason)
#endif
}

QThreadPool *inputThreadPool()
{
    static QThreadPool *pool = []
    {
        QThreadPool *createdPool = new QThreadPool();
        createdPool->setMaxThreadCount(1);
        createdPool->setExpiryTimeout(-1);
        return createdPool;
    }();
    return pool;
}

class SystemInputRunnable : public QRunnable
{
public:
    explicit SystemInputRunnable(const QList<InputActionRequest> &actions, quint64 dispatchId) :
        actions_(actions),
        dispatchId_(dispatchId)
    {
    }

    void run() override
    {
        qInfo().noquote() << QStringLiteral(
            "[capability.system.input] worker start requestId=%1 actions=%2"
        ).arg(dispatchId_).arg(actions_.size());

        int executedCount = 0;
        for ( int index = 0; index < actions_.size(); ++index )
        {
            if ( !isInputDispatchCurrent(dispatchId_) )
            {
                releaseTrackedPressedKeys(dispatchId_, inputCancelledReason());
                qInfo().noquote() << QStringLiteral(
                    "[capability.system.input] worker cancelled requestId=%1 executed=%2"
                ).arg(dispatchId_).arg(executedCount);
                return;
            }

            QString executeError;
            if ( !executeAction(actions_.at(index), dispatchId_, &executeError) )
            {
                const QString cleanupReason = executeError.isEmpty()
                    ? QStringLiteral("worker action failed")
                    : executeError;
                releaseTrackedPressedKeys(dispatchId_, cleanupReason);
                if ( executeError == inputCancelledReason() )
                {
                    qInfo().noquote() << QStringLiteral(
                        "[capability.system.input] worker cancelled requestId=%1 index=%2"
                    ).arg(dispatchId_).arg(index);
                    return;
                }

                qWarning().noquote() << QStringLiteral(
                    "[capability.system.input] worker failed requestId=%1 index=%2 error=%3"
                ).arg(dispatchId_).arg(index).arg(executeError);
                return;
            }
            ++executedCount;
        }

        releaseTrackedPressedKeys(dispatchId_, QStringLiteral("worker finished"));
        qInfo().noquote() << QStringLiteral(
            "[capability.system.input] worker done requestId=%1 actions=%2"
        ).arg(dispatchId_).arg(executedCount);
    }

private:
    QList<InputActionRequest> actions_;
    quint64 dispatchId_ = 0;
};

QString extractString(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString().trimmed() : QString();
}

bool parseIntValue(
    const QJsonValue &value,
    const QString &field,
    int minValue,
    int maxValue,
    int *parsedValue,
    QString *error
)
{
    if ( parsedValue == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.input internal error: integer output pointer is null");
        }
        return false;
    }

    if ( !value.isDouble() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.input %1 must be integer").arg(field);
        }
        return false;
    }

    const double rawValue = value.toDouble(std::numeric_limits<double>::quiet_NaN());
    if ( !std::isfinite(rawValue) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.input %1 must be integer").arg(field);
        }
        return false;
    }

    if ( ( rawValue < static_cast<double>((std::numeric_limits<int>::min)()) ) ||
         ( rawValue > static_cast<double>((std::numeric_limits<int>::max)()) ) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.input %1 out of range").arg(field);
        }
        return false;
    }

    const int integerValue = static_cast<int>(rawValue);
    if ( rawValue != static_cast<double>(integerValue) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.input %1 must be integer").arg(field);
        }
        return false;
    }

    if ( ( integerValue < minValue ) ||
         ( integerValue > maxValue ) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.input %1 out of range [%2, %3]")
                .arg(field)
                .arg(minValue)
                .arg(maxValue);
        }
        return false;
    }

    *parsedValue = integerValue;
    return true;
}

bool parseRequiredInt(
    const QJsonObject &object,
    const QString &field,
    int minValue,
    int maxValue,
    int *parsedValue,
    QString *error
)
{
    const QJsonValue value = object.value(field);
    if ( value.isUndefined() || value.isNull() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.input requires %1").arg(field);
        }
        return false;
    }
    return parseIntValue(value, field, minValue, maxValue, parsedValue, error);
}

bool parseOptionalInt(
    const QJsonObject &object,
    const QString &field,
    int minValue,
    int maxValue,
    int defaultValue,
    int *parsedValue,
    QString *error
)
{
    if ( parsedValue == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.input internal error: integer output pointer is null");
        }
        return false;
    }

    *parsedValue = defaultValue;
    const QJsonValue value = object.value(field);
    if ( value.isUndefined() || value.isNull() )
    {
        return true;
    }

    return parseIntValue(value, field, minValue, maxValue, parsedValue, error);
}

bool parseVirtualKey(const QString &inputKey, quint16 *virtualKey)
{
    if ( virtualKey == nullptr )
    {
        return false;
    }

#ifdef Q_OS_WIN
    *virtualKey = 0;
    const QString key = inputKey.trimmed().toUpper();
    if ( key.isEmpty() )
    {
        return false;
    }

    if ( key.size() == 1 )
    {
        const ushort unicode = key.at(0).unicode();
        if ( ( unicode >= 'A' ) && ( unicode <= 'Z' ) )
        {
            *virtualKey = static_cast<quint16>(unicode);
            return true;
        }
        if ( ( unicode >= '0' ) && ( unicode <= '9' ) )
        {
            *virtualKey = static_cast<quint16>(unicode);
            return true;
        }
    }

    if ( key.startsWith('F') )
    {
        bool ok = false;
        const int functionKeyNumber = key.mid(1).toInt(&ok);
        if ( ok &&
             ( functionKeyNumber >= 1 ) &&
             ( functionKeyNumber <= 24 ) )
        {
            *virtualKey = static_cast<quint16>(VK_F1 + ( functionKeyNumber - 1 ));
            return true;
        }
    }

    static const QHash<QString, quint16> keyMap =
    {
        {QStringLiteral("ENTER"), VK_RETURN},
        {QStringLiteral("RETURN"), VK_RETURN},
        {QStringLiteral("ESC"), VK_ESCAPE},
        {QStringLiteral("ESCAPE"), VK_ESCAPE},
        {QStringLiteral("TAB"), VK_TAB},
        {QStringLiteral("SPACE"), VK_SPACE},
        {QStringLiteral("BACKSPACE"), VK_BACK},
        {QStringLiteral("DELETE"), VK_DELETE},
        {QStringLiteral("INSERT"), VK_INSERT},
        {QStringLiteral("HOME"), VK_HOME},
        {QStringLiteral("END"), VK_END},
        {QStringLiteral("PAGEUP"), VK_PRIOR},
        {QStringLiteral("PGUP"), VK_PRIOR},
        {QStringLiteral("PAGEDOWN"), VK_NEXT},
        {QStringLiteral("PGDN"), VK_NEXT},
        {QStringLiteral("LEFT"), VK_LEFT},
        {QStringLiteral("RIGHT"), VK_RIGHT},
        {QStringLiteral("UP"), VK_UP},
        {QStringLiteral("DOWN"), VK_DOWN},
        {QStringLiteral("CTRL"), VK_CONTROL},
        {QStringLiteral("CONTROL"), VK_CONTROL},
        {QStringLiteral("LCTRL"), VK_LCONTROL},
        {QStringLiteral("RCTRL"), VK_RCONTROL},
        {QStringLiteral("ALT"), VK_MENU},
        {QStringLiteral("MENU"), VK_MENU},
        {QStringLiteral("LALT"), VK_LMENU},
        {QStringLiteral("RALT"), VK_RMENU},
        {QStringLiteral("SHIFT"), VK_SHIFT},
        {QStringLiteral("LSHIFT"), VK_LSHIFT},
        {QStringLiteral("RSHIFT"), VK_RSHIFT},
        {QStringLiteral("WIN"), VK_LWIN},
        {QStringLiteral("LWIN"), VK_LWIN},
        {QStringLiteral("RWIN"), VK_RWIN},
        {QStringLiteral("CAPSLOCK"), VK_CAPITAL},
        {QStringLiteral("NUMLOCK"), VK_NUMLOCK},
        {QStringLiteral("SCROLLLOCK"), VK_SCROLL},
        {QStringLiteral("PRINTSCREEN"), VK_SNAPSHOT},
        {QStringLiteral("PRTSC"), VK_SNAPSHOT},
        {QStringLiteral("PAUSE"), VK_PAUSE},
        {QStringLiteral("APPS"), VK_APPS},
        {QStringLiteral("CONTEXTMENU"), VK_APPS},
        {QStringLiteral("NUMPAD0"), VK_NUMPAD0},
        {QStringLiteral("NUMPAD1"), VK_NUMPAD1},
        {QStringLiteral("NUMPAD2"), VK_NUMPAD2},
        {QStringLiteral("NUMPAD3"), VK_NUMPAD3},
        {QStringLiteral("NUMPAD4"), VK_NUMPAD4},
        {QStringLiteral("NUMPAD5"), VK_NUMPAD5},
        {QStringLiteral("NUMPAD6"), VK_NUMPAD6},
        {QStringLiteral("NUMPAD7"), VK_NUMPAD7},
        {QStringLiteral("NUMPAD8"), VK_NUMPAD8},
        {QStringLiteral("NUMPAD9"), VK_NUMPAD9},
        {QStringLiteral("MULTIPLY"), VK_MULTIPLY},
        {QStringLiteral("ADD"), VK_ADD},
        {QStringLiteral("SUBTRACT"), VK_SUBTRACT},
        {QStringLiteral("DECIMAL"), VK_DECIMAL},
        {QStringLiteral("DIVIDE"), VK_DIVIDE},
        {QStringLiteral("SEMICOLON"), VK_OEM_1},
        {QStringLiteral("PLUS"), VK_OEM_PLUS},
        {QStringLiteral("COMMA"), VK_OEM_COMMA},
        {QStringLiteral("MINUS"), VK_OEM_MINUS},
        {QStringLiteral("PERIOD"), VK_OEM_PERIOD},
        {QStringLiteral("DOT"), VK_OEM_PERIOD},
        {QStringLiteral("SLASH"), VK_OEM_2},
        {QStringLiteral("BACKQUOTE"), VK_OEM_3},
        {QStringLiteral("TILDE"), VK_OEM_3},
        {QStringLiteral("LBRACKET"), VK_OEM_4},
        {QStringLiteral("BACKSLASH"), VK_OEM_5},
        {QStringLiteral("RBRACKET"), VK_OEM_6},
        {QStringLiteral("QUOTE"), VK_OEM_7},
        {QStringLiteral("APOSTROPHE"), VK_OEM_7},
    };

    const auto found = keyMap.constFind(key);
    if ( found == keyMap.constEnd() )
    {
        return false;
    }

    *virtualKey = found.value();
    return true;
#else
    Q_UNUSED(inputKey)
    *virtualKey = 0;
    return false;
#endif
}

bool parseInputAction(
    const QJsonObject &actionObject,
    int index,
    InputActionRequest *actionRequest,
    QString *error
)
{
    if ( actionRequest == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.input internal error: action output pointer is null");
        }
        return false;
    }

    *actionRequest = InputActionRequest();
    const QString actionType = extractString(actionObject, QStringLiteral("type")).toLower();
    if ( actionType.isEmpty() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.input actions[%1].type is required").arg(index);
        }
        return false;
    }

    actionRequest->typeName = actionType;

    if ( actionType == QStringLiteral("mouse.move") )
    {
        actionRequest->type = InputActionType::MouseMove;
        if ( !parseRequiredInt(
                actionObject,
                QStringLiteral("x"),
                (std::numeric_limits<int>::min)(),
                (std::numeric_limits<int>::max)(),
                &actionRequest->x,
                error
            ) )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("system.input actions[%1] %2")
                    .arg(index)
                    .arg(*error);
            }
            return false;
        }
        if ( !parseRequiredInt(
                actionObject,
                QStringLiteral("y"),
                (std::numeric_limits<int>::min)(),
                (std::numeric_limits<int>::max)(),
                &actionRequest->y,
                error
            ) )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("system.input actions[%1] %2")
                    .arg(index)
                    .arg(*error);
            }
            return false;
        }

        const QString mode = extractString(actionObject, QStringLiteral("mode")).toLower();
        actionRequest->moveMode = mode.isEmpty()
            ? QStringLiteral("absolute")
            : mode;
        if ( ( actionRequest->moveMode != QStringLiteral("absolute") ) &&
             ( actionRequest->moveMode != QStringLiteral("relative") ) )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral(
                    "system.input actions[%1] mouse.move mode must be absolute or relative"
                ).arg(index);
            }
            return false;
        }

        return true;
    }

    if ( actionType == QStringLiteral("mouse.click") )
    {
        actionRequest->type = InputActionType::MouseClick;
        const QString button = extractString(actionObject, QStringLiteral("button")).toLower();
        actionRequest->mouseButton = button.isEmpty()
            ? QStringLiteral("left")
            : button;
        if ( ( actionRequest->mouseButton != QStringLiteral("left") ) &&
             ( actionRequest->mouseButton != QStringLiteral("right") ) )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral(
                    "system.input actions[%1] mouse.click button must be left or right"
                ).arg(index);
            }
            return false;
        }

        if ( !parseOptionalInt(
                actionObject,
                QStringLiteral("count"),
                inputClickMinCount,
                inputClickMaxCount,
                1,
                &actionRequest->clickCount,
                error
            ) )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("system.input actions[%1] %2")
                    .arg(index)
                    .arg(*error);
            }
            return false;
        }

        return true;
    }

    if ( actionType == QStringLiteral("mouse.scroll") )
    {
        actionRequest->type = InputActionType::MouseScroll;
        const bool hasDelta = actionObject.contains(QStringLiteral("delta"));
        const bool hasDeltaY = actionObject.contains(QStringLiteral("deltaY"));
        if ( !hasDelta && !hasDeltaY )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral(
                    "system.input actions[%1] mouse.scroll requires delta or deltaY"
                ).arg(index);
            }
            return false;
        }
        if ( hasDelta && hasDeltaY )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral(
                    "system.input actions[%1] mouse.scroll cannot specify both delta and deltaY"
                ).arg(index);
            }
            return false;
        }

        const QString verticalField = hasDelta
            ? QStringLiteral("delta")
            : QStringLiteral("deltaY");
        if ( !parseRequiredInt(
                actionObject,
                verticalField,
                inputScrollMinDelta,
                inputScrollMaxDelta,
                &actionRequest->scrollDeltaY,
                error
            ) )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("system.input actions[%1] %2")
                    .arg(index)
                    .arg(*error);
            }
            return false;
        }

        if ( !parseOptionalInt(
                actionObject,
                QStringLiteral("deltaX"),
                inputScrollMinDelta,
                inputScrollMaxDelta,
                0,
                &actionRequest->scrollDeltaX,
                error
            ) )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("system.input actions[%1] %2")
                    .arg(index)
                    .arg(*error);
            }
            return false;
        }

        return true;
    }

    if ( actionType == QStringLiteral("mouse.drag") )
    {
        actionRequest->type = InputActionType::MouseDrag;
        if ( !parseRequiredInt(
                actionObject,
                QStringLiteral("x"),
                (std::numeric_limits<int>::min)(),
                (std::numeric_limits<int>::max)(),
                &actionRequest->x,
                error
            ) )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("system.input actions[%1] %2")
                    .arg(index)
                    .arg(*error);
            }
            return false;
        }
        if ( !parseRequiredInt(
                actionObject,
                QStringLiteral("y"),
                (std::numeric_limits<int>::min)(),
                (std::numeric_limits<int>::max)(),
                &actionRequest->y,
                error
            ) )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("system.input actions[%1] %2")
                    .arg(index)
                    .arg(*error);
            }
            return false;
        }

        const QString mode = extractString(actionObject, QStringLiteral("mode")).toLower();
        actionRequest->moveMode = mode.isEmpty()
            ? QStringLiteral("absolute")
            : mode;
        if ( ( actionRequest->moveMode != QStringLiteral("absolute") ) &&
             ( actionRequest->moveMode != QStringLiteral("relative") ) )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral(
                    "system.input actions[%1] mouse.drag mode must be absolute or relative"
                ).arg(index);
            }
            return false;
        }

        const QString button = extractString(actionObject, QStringLiteral("button")).toLower();
        actionRequest->mouseButton = button.isEmpty()
            ? QStringLiteral("left")
            : button;
        if ( ( actionRequest->mouseButton != QStringLiteral("left") ) &&
             ( actionRequest->mouseButton != QStringLiteral("right") ) )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral(
                    "system.input actions[%1] mouse.drag button must be left or right"
                ).arg(index);
            }
            return false;
        }

        return true;
    }

    if ( ( actionType == QStringLiteral("keyboard.down") ) ||
         ( actionType == QStringLiteral("keyboard.up") ) ||
         ( actionType == QStringLiteral("keyboard.tap") ) )
    {
        if ( actionType == QStringLiteral("keyboard.down") )
        {
            actionRequest->type = InputActionType::KeyboardDown;
        }
        else if ( actionType == QStringLiteral("keyboard.up") )
        {
            actionRequest->type = InputActionType::KeyboardUp;
        }
        else
        {
            actionRequest->type = InputActionType::KeyboardTap;
        }

        actionRequest->keyName = extractString(actionObject, QStringLiteral("key"));
        if ( actionRequest->keyName.isEmpty() )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("system.input actions[%1] keyboard key is required").arg(index);
            }
            return false;
        }

        if ( !parseVirtualKey(actionRequest->keyName, &actionRequest->virtualKey) )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral(
                    "system.input actions[%1] keyboard key \"%2\" is not supported"
                ).arg(index).arg(actionRequest->keyName);
            }
            return false;
        }

        return true;
    }

    if ( actionType == QStringLiteral("keyboard.text") )
    {
        actionRequest->type = InputActionType::KeyboardText;
        const QJsonValue textValue = actionObject.value(QStringLiteral("text"));
        if ( !textValue.isString() )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("system.input actions[%1] keyboard.text text must be string").arg(index);
            }
            return false;
        }

        actionRequest->text = textValue.toString();
        if ( actionRequest->text.isEmpty() )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("system.input actions[%1] keyboard.text text is empty").arg(index);
            }
            return false;
        }

        if ( !parseOptionalInt(
                actionObject,
                QStringLiteral("intervalMs"),
                inputDelayMinMs,
                inputTextIntervalMaxMs,
                0,
                &actionRequest->intervalMs,
                error
            ) )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("system.input actions[%1] %2")
                    .arg(index)
                    .arg(*error);
            }
            return false;
        }

        return true;
    }

    if ( actionType == QStringLiteral("delay") )
    {
        actionRequest->type = InputActionType::Delay;
        if ( !parseRequiredInt(
                actionObject,
                QStringLiteral("ms"),
                inputDelayMinMs,
                inputDelayMaxMs,
                &actionRequest->ms,
                error
            ) )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("system.input actions[%1] %2")
                    .arg(index)
                    .arg(*error);
            }
            return false;
        }

        return true;
    }

    if ( error != nullptr )
    {
        *error = QStringLiteral("system.input actions[%1] type \"%2\" is not supported")
            .arg(index)
            .arg(actionType);
    }
    return false;
}

bool parseInputRequest(
    const QJsonValue &params,
    QList<InputActionRequest> *actions,
    QString *error
)
{
    if ( actions == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.input internal error: actions output pointer is null");
        }
        return false;
    }

    actions->clear();
    if ( !params.isObject() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.input params must be object");
        }
        return false;
    }

    const QJsonObject paramsObject = params.toObject();
    const QJsonValue actionsValue = paramsObject.value(QStringLiteral("actions"));
    if ( !actionsValue.isArray() )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.input actions must be array");
        }
        return false;
    }

    const QJsonArray actionsArray = actionsValue.toArray();
    if ( ( actionsArray.size() < inputMinActions ) ||
         ( actionsArray.size() > inputMaxActions ) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.input actions count out of range [%1, %2]")
                .arg(inputMinActions)
                .arg(inputMaxActions);
        }
        return false;
    }

    for ( int index = 0; index < actionsArray.size(); ++index )
    {
        const QJsonValue actionValue = actionsArray.at(index);
        if ( !actionValue.isObject() )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("system.input actions[%1] must be object").arg(index);
            }
            return false;
        }

        InputActionRequest request;
        QString parseError;
        if ( !parseInputAction(actionValue.toObject(), index, &request, &parseError) )
        {
            if ( error != nullptr )
            {
                *error = parseError;
            }
            return false;
        }
        actions->append(request);
    }

    return true;
}

#ifdef Q_OS_WIN

QString win32ErrorMessage(DWORD errorCode)
{
    LPWSTR buffer = nullptr;
    const DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr
    );
    if ( ( size == 0 ) || ( buffer == nullptr ) )
    {
        return QStringLiteral("win32 error %1").arg(QString::number(errorCode));
    }

    QString message = QString::fromWCharArray(buffer, static_cast<int>(size)).trimmed();
    LocalFree(buffer);
    if ( message.isEmpty() )
    {
        return QStringLiteral("win32 error %1").arg(QString::number(errorCode));
    }
    return QStringLiteral("%1 (code=%2)")
        .arg(message)
        .arg(errorCode);
}

bool sendMouseMoveAbsolute(int x, int y, QString *error)
{
    const int virtualLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int virtualTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int virtualWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int virtualHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if ( ( virtualWidth <= 1 ) || ( virtualHeight <= 1 ) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("invalid virtual screen metrics");
        }
        return false;
    }

    LONG normalizedX = static_cast<LONG>( std::llround(
        ( static_cast<double>(static_cast<qint64>(x) - static_cast<qint64>(virtualLeft)) * 65535.0 ) /
        static_cast<double>(virtualWidth - 1)
    ) );
    LONG normalizedY = static_cast<LONG>( std::llround(
        ( static_cast<double>(static_cast<qint64>(y) - static_cast<qint64>(virtualTop)) * 65535.0 ) /
        static_cast<double>(virtualHeight - 1)
    ) );
    normalizedX = qBound(0L, normalizedX, 65535L);
    normalizedY = qBound(0L, normalizedY, 65535L);

    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dx = normalizedX;
    input.mi.dy = normalizedY;
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;

    if ( SendInput(1, &input, sizeof(INPUT)) != 1 )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("failed to send absolute mouse move: %1")
                .arg(win32ErrorMessage(GetLastError()));
        }
        return false;
    }

    return true;
}

bool sendMouseMoveRelative(int x, int y, QString *error)
{
    POINT cursorPosition = {};
    if ( !GetCursorPos(&cursorPosition) )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("failed to read cursor position for relative move: %1")
                .arg(win32ErrorMessage(GetLastError()));
        }
        return false;
    }

    // Use an absolute target to avoid Windows pointer acceleration amplifying deltas.
    const qint64 targetX = static_cast<qint64>(cursorPosition.x) + static_cast<qint64>(x);
    const qint64 targetY = static_cast<qint64>(cursorPosition.y) + static_cast<qint64>(y);
    const int boundedX = static_cast<int>(qBound(
        static_cast<qint64>((std::numeric_limits<int>::min)()),
        targetX,
        static_cast<qint64>((std::numeric_limits<int>::max)())
    ));
    const int boundedY = static_cast<int>(qBound(
        static_cast<qint64>((std::numeric_limits<int>::min)()),
        targetY,
        static_cast<qint64>((std::numeric_limits<int>::max)())
    ));

    return sendMouseMoveAbsolute(boundedX, boundedY, error);
}

bool sendMouseButtonEvent(const QString &button, bool down, QString *error)
{
    const bool isLeftButton = ( button == QStringLiteral("left") );
    const DWORD eventFlag = isLeftButton
        ? ( down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP )
        : ( down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP );

    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = eventFlag;
    if ( SendInput(1, &input, sizeof(INPUT)) != 1 )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("failed to send mouse button event: %1")
                .arg(win32ErrorMessage(GetLastError()));
        }
        return false;
    }

    return true;
}

bool sendMouseClick(const QString &button, int count, quint64 dispatchId, QString *error)
{
    for ( int i = 0; i < count; ++i )
    {
        if ( !isInputDispatchCurrent(dispatchId) )
        {
            return setCancelledError(error);
        }

        QString clickError;
        if ( !sendMouseButtonEvent(button, true, &clickError) ||
             !sendMouseButtonEvent(button, false, &clickError) )
        {
            if ( error != nullptr )
            {
                *error = clickError;
            }
            return false;
        }
    }

    return true;
}

bool sendMouseScroll(int deltaX, int deltaY, quint64 dispatchId, QString *error)
{
    if ( !isInputDispatchCurrent(dispatchId) )
    {
        return setCancelledError(error);
    }

    INPUT inputs[2] = {};
    UINT inputCount = 0;
    if ( deltaY != 0 )
    {
        inputs[inputCount].type = INPUT_MOUSE;
        inputs[inputCount].mi.dwFlags = MOUSEEVENTF_WHEEL;
        inputs[inputCount].mi.mouseData = static_cast<DWORD>(static_cast<LONG>(deltaY));
        ++inputCount;
    }
    if ( deltaX != 0 )
    {
        inputs[inputCount].type = INPUT_MOUSE;
        inputs[inputCount].mi.dwFlags = MOUSEEVENTF_HWHEEL;
        inputs[inputCount].mi.mouseData = static_cast<DWORD>(static_cast<LONG>(deltaX));
        ++inputCount;
    }

    if ( inputCount == 0 )
    {
        return true;
    }

    if ( SendInput(inputCount, inputs, sizeof(INPUT)) != inputCount )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("failed to send mouse scroll: %1")
                .arg(win32ErrorMessage(GetLastError()));
        }
        return false;
    }

    return true;
}

bool sendMouseDrag(
    const QString &button,
    const QString &mode,
    int x,
    int y,
    quint64 dispatchId,
    QString *error
)
{
    if ( !isInputDispatchCurrent(dispatchId) )
    {
        return setCancelledError(error);
    }

    if ( !sendMouseButtonEvent(button, true, error) )
    {
        return false;
    }

    if ( !isInputDispatchCurrent(dispatchId) )
    {
        QString releaseError;
        if ( !sendMouseButtonEvent(button, false, &releaseError) )
        {
            qWarning().noquote() << QStringLiteral(
                "[capability.system.input] failed to release mouse button after drag cancellation: %1"
            ).arg(releaseError);
        }
        return setCancelledError(error);
    }

    QString dragError;
    const bool moveOk = ( mode == QStringLiteral("relative") )
        ? sendMouseMoveRelative(x, y, &dragError)
        : sendMouseMoveAbsolute(x, y, &dragError);
    if ( !moveOk )
    {
        QString releaseError;
        if ( !sendMouseButtonEvent(button, false, &releaseError) )
        {
            qWarning().noquote() << QStringLiteral(
                "[capability.system.input] failed to release mouse button after drag move failure: %1"
            ).arg(releaseError);
        }
        if ( error != nullptr )
        {
            *error = dragError;
        }
        return false;
    }

    const bool cancelled = !isInputDispatchCurrent(dispatchId);
    if ( !sendMouseButtonEvent(button, false, error) )
    {
        return false;
    }

    if ( cancelled )
    {
        return setCancelledError(error);
    }

    return true;
}

bool sendKeyboardVirtualKey(quint16 virtualKey, bool keyUp, QString *error)
{
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = virtualKey;
    input.ki.dwFlags = keyUp ? KEYEVENTF_KEYUP : 0;

    if ( SendInput(1, &input, sizeof(INPUT)) != 1 )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("failed to send keyboard event: %1")
                .arg(win32ErrorMessage(GetLastError()));
        }
        return false;
    }
    return true;
}

bool sleepInterruptible(int ms, quint64 dispatchId, QString *error)
{
    int remaining = ms;
    while ( remaining > 0 )
    {
        if ( !isInputDispatchCurrent(dispatchId) )
        {
            return setCancelledError(error);
        }
        const int step = qMin(remaining, inputCancellationPollMs);
        QThread::msleep(static_cast<unsigned long>(step));
        remaining -= step;
    }

    if ( !isInputDispatchCurrent(dispatchId) )
    {
        return setCancelledError(error);
    }
    return true;
}

bool sendKeyboardTap(quint16 virtualKey, quint64 dispatchId, QString *error)
{
    if ( !isInputDispatchCurrent(dispatchId) )
    {
        return setCancelledError(error);
    }
    if ( !sendKeyboardVirtualKey(virtualKey, false, error) )
    {
        return false;
    }

    if ( !sleepInterruptible(inputTapHoldMs, dispatchId, error) )
    {
        QString releaseError;
        if ( !sendKeyboardVirtualKey(virtualKey, true, &releaseError) )
        {
            qWarning().noquote() << QStringLiteral(
                "[capability.system.input] failed to release key after tap interruption: %1"
            ).arg(releaseError);
        }
        return false;
    }

    return sendKeyboardVirtualKey(virtualKey, true, error);
}

bool sendKeyboardText(
    const QString &text,
    int intervalMs,
    quint64 dispatchId,
    QString *error
)
{
    for ( int charIndex = 0; charIndex < text.size(); ++charIndex )
    {
        if ( !isInputDispatchCurrent(dispatchId) )
        {
            return setCancelledError(error);
        }

        INPUT inputs[2] = {};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wScan = text.at(charIndex).unicode();
        inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;
        inputs[1] = inputs[0];
        inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

        if ( SendInput(2, inputs, sizeof(INPUT)) != 2 )
        {
            if ( error != nullptr )
            {
                *error = QStringLiteral("failed to send keyboard text: %1")
                    .arg(win32ErrorMessage(GetLastError()));
            }
            return false;
        }

        if ( ( intervalMs > 0 ) &&
             ( charIndex + 1 < text.size() ) )
        {
            if ( !sleepInterruptible(intervalMs, dispatchId, error) )
            {
                return false;
            }
        }
    }
    return true;
}

bool executeAction(
    const InputActionRequest &action,
    quint64 dispatchId,
    QString *error
)
{
    if ( !isInputDispatchCurrent(dispatchId) )
    {
        return setCancelledError(error);
    }

    switch ( action.type )
    {
    case InputActionType::MouseMove:
        if ( action.moveMode == QStringLiteral("relative") )
        {
            return sendMouseMoveRelative(action.x, action.y, error);
        }
        return sendMouseMoveAbsolute(action.x, action.y, error);
    case InputActionType::MouseClick:
        return sendMouseClick(action.mouseButton, action.clickCount, dispatchId, error);
    case InputActionType::MouseScroll:
        return sendMouseScroll(action.scrollDeltaX, action.scrollDeltaY, dispatchId, error);
    case InputActionType::MouseDrag:
        return sendMouseDrag(
            action.mouseButton,
            action.moveMode,
            action.x,
            action.y,
            dispatchId,
            error
        );
    case InputActionType::KeyboardDown:
        if ( !sendKeyboardVirtualKey(action.virtualKey, false, error) )
        {
            return false;
        }
        trackPressedKey(dispatchId, action.virtualKey);
        return true;
    case InputActionType::KeyboardUp:
        if ( !sendKeyboardVirtualKey(action.virtualKey, true, error) )
        {
            return false;
        }
        untrackPressedKey(dispatchId, action.virtualKey);
        return true;
    case InputActionType::KeyboardTap:
        return sendKeyboardTap(action.virtualKey, dispatchId, error);
    case InputActionType::KeyboardText:
        return sendKeyboardText(action.text, action.intervalMs, dispatchId, error);
    case InputActionType::Delay:
        return sleepInterruptible(action.ms, dispatchId, error);
    }

    if ( error != nullptr )
    {
        *error = QStringLiteral("unknown action type");
    }
    return false;
}

#else

bool executeAction(
    const InputActionRequest &action,
    quint64 dispatchId,
    QString *error
)
{
    Q_UNUSED(action)
    Q_UNUSED(dispatchId)
    if ( error != nullptr )
    {
        *error = QStringLiteral("system.input is only supported on Windows");
    }
    return false;
}

#endif // Q_OS_WIN
}

bool SystemInput::execute(
    const QJsonValue &params,
    QJsonObject *result,
    QString *error,
    bool *invalidParams
)
{
    if ( invalidParams != nullptr )
    {
        *invalidParams = false;
    }

    if ( result == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.input output pointer is null");
        }
        return false;
    }

    QList<InputActionRequest> actions;
    QString parseError;
    if ( !parseInputRequest(params, &actions, &parseError) )
    {
        if ( invalidParams != nullptr )
        {
            *invalidParams = true;
        }
        if ( error != nullptr )
        {
            *error = parseError;
        }
        return false;
    }

#ifndef Q_OS_WIN
    if ( error != nullptr )
    {
        *error = QStringLiteral("system.input is only supported on Windows");
    }
    return false;
#endif

    qInfo().noquote() << QStringLiteral(
        "[capability.system.input] dispatch actions=%1"
    ).arg(actions.size());

    QThreadPool *pool = inputThreadPool();
    if ( pool == nullptr )
    {
        if ( error != nullptr )
        {
            *error = QStringLiteral("system.input thread pool is unavailable");
        }
        return false;
    }

    quint64 dispatchId = 0;
    {
        QMutexLocker locker(&inputDispatchMutex());
        dispatchId = markLatestInputDispatch();
        pool->clear();

        SystemInputRunnable *runnable = new SystemInputRunnable(actions, dispatchId);
        pool->start(runnable);
    }

    QJsonObject out;
    out.insert(QStringLiteral("operation"), QStringLiteral("input"));
    out.insert(QStringLiteral("totalCount"), actions.size());
    out.insert(QStringLiteral("accepted"), true);
    out.insert(QStringLiteral("async"), true);
    out.insert(QStringLiteral("ok"), true);
    *result = out;

    qInfo().noquote() << QStringLiteral(
        "[capability.system.input] dispatch done requestId=%1 actions=%2 activeThreads=%3 maxThreads=%4"
    ).arg(dispatchId).arg(actions.size()).arg(pool->activeThreadCount()).arg(pool->maxThreadCount());
    return true;
}
