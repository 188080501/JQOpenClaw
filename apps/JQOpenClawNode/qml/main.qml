import QtQuick
import QtQuick.Window
import JQControls
import "./"

JQWindow {
    id: window
    width: 560
    height: 700
    minimumWidth: 560
    maximumWidth: 560
    visible: true
    opacity: 0
    title: qsTr("JQOpenClawNode")
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

            InfoPanel {
                width: parent.width
            }

            ConfigPanel {
                width: parent.width
            }

            PermissionPanel {
                width: parent.width
            }

            SoftwarePanel {
                width: parent.width
            }

            HistoryPanel {
                width: parent.width
            }
        }
    }
}
