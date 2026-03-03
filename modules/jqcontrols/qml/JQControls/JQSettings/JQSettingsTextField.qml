import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import "./"
import "../"

JQSettingsRow {
    id: jqSettingsTextField

    property alias text:           jqTextField.text 
    property alias defaultText:    jqTextField.defaultText
    property alias maximumLength:  jqTextField.maximumLength
    property alias readOnly:       jqTextField.readOnly
    property alias echoMode:       jqTextField.echoMode
    property alias copyEnabled:    jqTextField.copyEnabled
    property alias textFieldWidth: jqTextField.width

    JQTextField {
        id: jqTextField
        anchors.verticalCenter: parent.verticalCenter

        ToolTip.visible: hovered && enabled && ( jqSettingsTextField.tipText !== "" )
        ToolTip.text: jqSettingsTextField.tipText
    }
}


