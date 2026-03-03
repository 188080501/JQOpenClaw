import QtQuick
import JQControls
import JQOpenClawNode

JQPane {
    id: infoPane
    height: contentColumn.height + 40

    Column {
        id: contentColumn
        x: 12
        y: 12
        width: parent.width - 24
        spacing: 8

        Text {
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
            text: qsTr("最后调用: %1  %2")
                .arg(nodeApplication.lastInvokeTime)
                .arg(nodeApplication.lastInvokeCapability)
            font.pixelSize: 14
            color: "#1f2937"
        }
    }
}
