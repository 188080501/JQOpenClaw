import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import "./"
import "../"

JQSettingsRow {
    id: jqSettingsComboBox

    property alias model:         jqComboBox.model
    property alias currentIndex:  jqComboBox.currentIndex
    property alias textRole:      jqComboBox.textRole
    property alias valueRole:     jqComboBox.valueRole
    property alias comboBoxWidth: jqComboBox.width
    readonly property string currentText: jqComboBox.currentText

    JQComboBox {
        id: jqComboBox
        anchors.verticalCenter: parent.verticalCenter

        ToolTip.visible: hovered && enabled && ( jqSettingsComboBox.tipText !== "" )
        ToolTip.text: jqSettingsComboBox.tipText
    }
}


