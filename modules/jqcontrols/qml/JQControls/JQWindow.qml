import QtQuick
import QtQuick.Window
import "./"

Window {
    id: jqWindow
    width: 1920
    height: 1000
    minimumWidth: 640
    minimumHeight: 480
    color: "#f4f4f4"
    visible: false

    // ∂‘Õ‚ Ù–‘
    property bool enabledAutoShowMaximized: true

    property int autoMaximizedBindWidth: 1920

    Component.onCompleted: {
        JQGlobal.window = this;

        if ( enabledAutoShowMaximized )
        {
            if ( Screen.desktopAvailableWidth <= autoMaximizedBindWidth )
            {
                Qt.callLater( ()=>{ showMaximized(); } );
            }
            else
            {
                Qt.callLater( ()=>{ visible = true; } );
            }
        }
        else
        {
            visible = true;
        }
    }
}
