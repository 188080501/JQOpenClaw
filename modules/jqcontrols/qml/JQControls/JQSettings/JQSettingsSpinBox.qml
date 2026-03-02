import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import "./"
import "../"

JQSettingsRow {
    id: jqSettingsSpinBox

    property alias value:         jqSpinBox.value
    property alias defaultValue:  jqSpinBox.defaultValue
    property alias currentValue:  jqSpinBox.currentValue
    property alias stepSize:      jqSpinBox.stepSize
    property alias from:          jqSpinBox.from
    property alias to:            jqSpinBox.to
    property alias decimals:      jqSpinBox.decimals
    property alias decimalFactor: jqSpinBox.decimalFactor
    property alias labelText:     jqSpinBox.labelText
    property alias suffixText:    jqSpinBox.suffixText
    property alias spinBoxWidth:  jqSpinBox.width

    function addStep(incrementValue) {
        jqSpinBox.addStep( incrementValue );
    }

    function minusStep(incrementValue) {
        jqSpinBox.minusStep( incrementValue );
    }

    signal valueApply()

    JQSpinBox {
        id: jqSpinBox
        anchors.verticalCenter: parent.verticalCenter

        ToolTip.visible: hovered && enabled && ( jqSettingsSpinBox.tipText !== "" )
        ToolTip.text: jqSettingsSpinBox.tipText

        onValueApply: {
            jqSettingsSpinBox.valueApply();
        }
    }
}


