import QtQuick
import JQControls

JQPane {
    id: softwarePane
    height: contentColumn.height + 40
    property bool applyingConfig: false

    function readBool(configObject, key, defaultValue) {
        if ( configObject && ( typeof configObject[key] === "boolean" ) )
        {
            return configObject[key];
        }
        return defaultValue;
    }

    function applyConfig(configObject) {
        applyingConfig = true;
        followSystemStartup.checked = readBool(
            configObject,
            "followSystemStartup",
            false
        );
        silentStartup.checked = readBool(
            configObject,
            "silentStartup",
            false
        );
        applyingConfig = false;
    }

    Component.onCompleted: {
        applyConfig( nodeApplication.config );
    }

    Connections {
        target: nodeApplication

        function onConfigChanged(config) {
            applyConfig( config );
        }
    }

    Column {
        id: contentColumn
        x: 12
        y: 12
        width: parent.width - 24
        spacing: 8

        Text {
            text: qsTr("软件设置")
            font.pixelSize: 20
            font.bold: true
            color: "#111827"
        }

        JQSettingsCheckBox {
            id: followSystemStartup
            titleText: qsTr("跟随系统启动")
            tipText: qsTr("开启后将在当前用户登录系统时自动启动 JQOpenClawNode")
            titleWidth: 170

            onCheckedChanged: {
                if ( softwarePane.applyingConfig || !nodeApplication )
                {
                    return;
                }

                if ( !nodeApplication.setFollowSystemStartup( checked ) )
                {
                    JQGlobal.showMessage( qsTr("保存失败，请查看日志") );
                    applyConfig( nodeApplication.config );
                    return;
                }

                JQGlobal.showMessage( qsTr("设置已保存") );
            }
        }

        JQSettingsCheckBox {
            id: silentStartup
            titleText: qsTr("静默启动")
            tipText: qsTr("开启后程序启动时不显示主界面，仅驻留系统托盘（下次启动生效）")
            titleWidth: 170

            onCheckedChanged: {
                if ( softwarePane.applyingConfig || !nodeApplication )
                {
                    return;
                }

                if ( !nodeApplication.setSilentStartup( checked ) )
                {
                    JQGlobal.showMessage( qsTr("保存失败，请查看日志") );
                    applyConfig( nodeApplication.config );
                    return;
                }

                JQGlobal.showMessage( qsTr("设置已保存，下次启动生效") );
            }
        }
    }
}
