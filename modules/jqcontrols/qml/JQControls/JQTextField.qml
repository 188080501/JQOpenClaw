import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

TextField {
    id: jqTextField
    width: 200
    height: 40
    opacity: enabled ? 1 : 0.5
    leftPadding: 6
    rightPadding: 6
    readOnly: false
    selectByMouse: true
    text: defaultText

    property string defaultText
}

