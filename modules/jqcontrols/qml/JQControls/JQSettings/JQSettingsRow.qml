import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import "./"
import "../"

Row {
    id: jqSettingsRow

    property string titleText
    property int    titleWidth: 120
    property string tipText

    Text {
        id: jqLabel
        width: jqSettingsRow.titleWidth
        height: 40
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
        text: jqSettingsRow.titleText + ":"
    }
}


