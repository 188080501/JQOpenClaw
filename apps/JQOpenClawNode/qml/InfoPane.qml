import QtQuick
import JQControls

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
            text: qsTr("设备状态")
            font.pixelSize: 18
            font.bold: true
            color: "#111827"
        }

        Text {
            text: qsTr("连接状态: %1").arg(nodeApplication.connectionStateText)
            font.pixelSize: 14
            color: "#1f2937"
        }

        Text {
            text: qsTr("最后调用: %1").arg(nodeApplication.lastInvokeTime)
            font.pixelSize: 14
            color: "#1f2937"
        }
    }
}
