import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import "./"
import "../"

JQSettingsRow {
    id: jqSettingsSlider

    property alias value:        jqSlider.value
    property alias defaultValue: jqSlider.defaultValue
    property alias from:         jqSlider.from
    property alias to:           jqSlider.to
    property alias stepSize:     jqSlider.stepSize
    property alias sliderWidth:  jqSlider.width

    JQSlider {
        id: jqSlider
        anchors.verticalCenter: parent.verticalCenter
        width: 150
        leftPadding: 10
        rightPadding: 10

        ToolTip.visible: hovered && enabled && ( jqSettingsSlider.tipText !== "" )
        ToolTip.text: jqSettingsSlider.tipText

        onValueChanged: {
            if ( jqValueSpinBox.value !== value )
            {
                jqValueSpinBox.value = value;
            }
        }
    }

    JQSpinBox {
        id: jqValueSpinBox
        anchors.verticalCenter: parent.verticalCenter
        width: 60
        stepButtonVisible: false
        from: jqSlider.from
        to: jqSlider.to
        stepSize: jqSlider.stepSize

        onValueApply: {
            if ( jqSlider.value !== value )
            {
                jqSlider.value = value;
            }
        }

        Component.onCompleted: {
            value = jqSlider.value;
        }
    }
}


