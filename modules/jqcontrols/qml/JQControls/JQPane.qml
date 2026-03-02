import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

Pane {
    id: jqPane
    Material.elevation: 2
    Material.background: "#fdfdfd"

    MouseArea {
        anchors.fill: parent

        onClicked: {
            forceActiveFocus();
        }
    }
}
