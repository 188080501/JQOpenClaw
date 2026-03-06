import QtQuick
import JQControls

JQPane {
    id: historyPane    
    height: listColumn.height + 40

    readonly property int panePadding: 12
    readonly property int listTopSpacing: 8
    readonly property int rowHeight: 30
    readonly property int rowSpacing: 4
    readonly property int maxVisibleRows: 10

    property var historyRecords: []

    function readHistory() {
        if ( !nodeApplication )
        {
            historyRecords = [];
            return;
        }

        historyRecords = nodeApplication.invokeHistory || [];
    }

    Component.onCompleted: {
        readHistory();
    }

    Connections {
        target: nodeApplication

        function onInvokeHistoryChanged(invokeHistory) {
            historyPane.historyRecords = invokeHistory || [];
        }
    }

    Column {
        id: listColumn
        x: 12
        y: 12
        width: parent.width - 24
        spacing: 8

        Text {
            text: qsTr("调用记录（最近10次）")
            font.pixelSize: 20
            font.bold: true
            color: "#111827"
        }

        Item {
            width: 1
            height: 1
        }

        Repeater {
            model: 10

            Item {
                width: parent.width
                height: rowHeight

                readonly property var historyItem: ( index < historyPane.historyRecords.length )
                    ? historyPane.historyRecords[index]
                    : null

                Rectangle {
                    anchors.fill: parent
                    radius: 6
                    color: "#f3f4f6"
                    border.width: 1
                    border.color: "#e5e7eb"
                    visible: index < historyPane.historyRecords.length

                    Row {
                        id: itemRow
                        x: 10
                        y: Math.round(( parent.height - height ) / 2)
                        width: parent.width - 20
                        spacing: 12

                        Text {
                            width: 170
                            text: ( historyItem && historyItem.time )
                                ? historyItem.time
                                : qsTr("未知时间")
                            font.pixelSize: 13
                            color: "#4b5563"
                            verticalAlignment: Text.AlignVCenter
                        }

                        Text {
                            width: itemRow.width - 170 - itemRow.spacing
                            text: ( historyItem && historyItem.capability )
                                ? historyItem.capability
                                : qsTr("未知能力")
                            font.pixelSize: 14
                            color: "#111827"
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }

    Text {
        anchors.centerIn: parent
        visible: historyPane.historyRecords.length === 0
        text: qsTr("暂无调用记录")
        font.pixelSize: 20
        color: "#111827"
    }
}
