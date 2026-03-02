import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import "./"
import "../"

JQSettingsRow {
    id: jqSettingsCheckBox

    property alias checked:      jqCheckBox.checked
    property alias defaultValue: jqCheckBox.defaultValue

    Item {
        width: -20
        height: 1
    }

    JQCheckBox {
        id: jqCheckBox
        anchors.verticalCenter: parent.verticalCenter

        ToolTip.visible: hovered && enabled && ( jqSettingsCheckBox.tipText !== "" )
        ToolTip.text: jqSettingsCheckBox.tipText
    }
}


