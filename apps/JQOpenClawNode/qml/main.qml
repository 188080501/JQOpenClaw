import QtQuick
import QtQuick.Window
import JQControls

Window {
    id: window
    width: 720
    height: 640
    minimumWidth: 720
    minimumHeight: 640
    visible: true
    opacity: 0
    title: "JQOpenClawNode"
    color: "#f5f6f8"

    Component.onCompleted: {
        startupOpacityAnimation.start();
    }

    NumberAnimation {
        id: startupOpacityAnimation
        target: window
        property: "opacity"
        easing.type: Easing.OutCubic
        duration: 300
        to: 1
    }
}
