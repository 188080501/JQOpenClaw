import QtQuick
import QtQuick.Window
import JQControls
import "./"

Window {
    id: window
    width: 600
    height: 680
    minimumWidth: 600
    minimumHeight: 100
    maximumWidth: 600
    visible: true
    opacity: 0
    title: qsTr("JQOpenClawNode")
    color: "#f5f6f8"

    Component.onCompleted: {
        JQGlobal.window = window;
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

    JQFlickable {
        id: pageFlickable
        anchors.fill: parent
        contentWidth: width
        contentHeight: Math.max( height, contentColumn.implicitHeight + 40 )

        Column {
            id: contentColumn
            x: 20
            y: 20
            width: pageFlickable.width - 40
            spacing: 12

            InfoPane {
                width: parent.width
            }

            ConfigPane {
                width: parent.width
            }
        }
    }
}
