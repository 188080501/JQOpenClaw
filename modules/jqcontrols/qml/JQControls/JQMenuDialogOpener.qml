import QtQuick
import QtQuick.Controls
import JQControls

JQMenuItem {
    id: jqMenuDialogOpener

    property url dialogUrl
    property var dialogProperties

    onClicked: {
        JQGlobal.createObjectAndOpen( dialogUrl, dialogProperties )
    }
}

