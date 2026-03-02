import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

Slider {
    id: jqSlider
    width: 240
    height: 40
    from: 0
    to: 100
    stepSize: 1
    value: defaultValue
    Material.accent: Material.LightBlue

    property real defaultValue: from
}

