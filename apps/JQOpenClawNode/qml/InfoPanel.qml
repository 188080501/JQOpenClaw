import QtQuick
import JQControls
import JQOpenClawNode

JQPane {
    id: infoPanel
    height: contentColumn.height + 40
    readonly property string openClawRepoUrl: "https://github.com/openclaw/openclaw"
    readonly property string jqOpenClawRepoUrl: "https://github.com/188080501/JQOpenClaw"

    Column {
        id: contentColumn
        x: 12
        y: 12
        width: parent.width - 24
        spacing: 12

        Text {
            width: parent.width
            wrapMode: Text.Wrap
            font.pixelSize: 18
            text: qsTr("连接状态: %1").arg(nodeApplication.connectionStateText)
            color: {
                switch (nodeApplication.connectionState) {
                case NodeApplication.Disconnected:
                    return "#6b7280"
                case NodeApplication.Connecting:
                    return "#b45309"
                case NodeApplication.Pairing:
                    return "#2563eb"
                case NodeApplication.Connected:
                    return "#16a34a"
                case NodeApplication.Error:
                    return "#b91c1c"
                default:
                    return "#6b7280"
                }
            }
        }

        Text {
            text: qsTr("启动时间: %1").arg(nodeApplication.startupTime)
            font.pixelSize: 14
            color: "#1f2937"
        }

        Row {
            spacing: 16

            Text {
                id: openClawRepoText
                text: qsTr("访问OpenClaw")
                font.pixelSize: 14
                color: "#2563eb"
                font.underline: openClawRepoMouseArea.containsMouse

                MouseArea {
                    id: openClawRepoMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: Qt.openUrlExternally(infoPanel.openClawRepoUrl)
                }
            }

            Text {
                id: jqOpenClawRepoText
                text: qsTr("访问JQOpenClaw")
                font.pixelSize: 14
                color: "#2563eb"
                font.underline: jqOpenClawRepoMouseArea.containsMouse

                MouseArea {
                    id: jqOpenClawRepoMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: Qt.openUrlExternally(infoPanel.jqOpenClawRepoUrl)
                }
            }
        }
    }

    Text {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        text: qsTr("当前版本: %1").arg(Qt.application.version)
        font.pixelSize: 14
        color: "#9ca3af"
    }
}
