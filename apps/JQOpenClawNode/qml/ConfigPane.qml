import QtQuick
import JQControls

JQPane {
    id: configPane
    height: contentColumn.height + 40

    function readString(configObject, key, defaultValue) {
        if ( configObject && ( typeof configObject[key] === "string" ) )
        {
            return configObject[key];
        }
        return defaultValue;
    }

    function readBool(configObject, key, defaultValue) {
        if ( configObject && ( typeof configObject[key] === "boolean" ) )
        {
            return configObject[key];
        }
        return defaultValue;
    }

    function readInt(configObject, key, defaultValue) {
        if ( !configObject )
        {
            return defaultValue;
        }

        var rawValue = Number( configObject[key] );
        if ( !isFinite( rawValue ) )
        {
            return defaultValue;
        }

        rawValue = Math.floor( rawValue );
        if ( rawValue < 1 )
        {
            return defaultValue;
        }
        if ( rawValue > 65535 )
        {
            return 65535;
        }
        return rawValue;
    }

    function applyConfig(configObject) {
        gatewayHost.text = readString( configObject, "host", "127.0.0.1" );
        gatewayPort.value = readInt( configObject, "port", 18789 );
        gatewayToken.text = readString( configObject, "token", "" );
        tlsEnabled.checked = readBool( configObject, "tls", false );
        displayName.text = readString( configObject, "displayName", "" );
        nodeId.text = readString( configObject, "nodeId", "" );
        identityPath.text = readString( configObject, "identityPath", "" );
        fileServerUri.text = readString( configObject, "fileServerUri", "" );
        fileServerToken.text = readString( configObject, "fileServerToken", "" );
        modelIdentifier.text = readString( configObject, "modelIdentifier", "" );
    }

    function buildConfig() {
        return {
            host: gatewayHost.text.trim(),
            port: gatewayPort.value,
            token: gatewayToken.text,
            tls: tlsEnabled.checked,
            displayName: displayName.text.trim(),
            nodeId: nodeId.text.trim(),
            identityPath: identityPath.text.trim(),
            fileServerUri: fileServerUri.text.trim(),
            fileServerToken: fileServerToken.text,
            modelIdentifier: modelIdentifier.text.trim()
        };
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
        spacing: 12

        Text {
            text: qsTr("参数设置")
            font.pixelSize: 20
            font.bold: true
            color: "#111827"
        }

        Column {
            width: parent.width
            spacing: 8

            JQSettingsTextField {
                id: gatewayHost
                titleText: qsTr("网关地址")
                titleWidth: 170
                textFieldWidth: parent.width - 170 - 24
            }

            JQSettingsCheckBox {
                id: tlsEnabled
                titleText: qsTr("启用TLS")
                titleWidth: 170
            }

            JQSettingsSpinBox {
                id: gatewayPort
                titleText: qsTr("网关端口")
                titleWidth: 170
                from: 1
                to: 65535
                value: 18789
            }

            JQSettingsTextField {
                id: gatewayToken
                titleText: qsTr("网关Token")
                titleWidth: 170
                textFieldWidth: parent.width - 170 - 24
            }

            JQSettingsTextField {
                id: displayName
                titleText: qsTr("显示名称")
                titleWidth: 170
                textFieldWidth: parent.width - 170 - 24
            }

            JQSettingsTextField {
                id: nodeId
                titleText: qsTr("实例ID")
                titleWidth: 170
                textFieldWidth: parent.width - 170 - 24
            }

            JQSettingsTextField {
                id: identityPath
                titleText: qsTr("身份文件路径")
                titleWidth: 170
                textFieldWidth: parent.width - 170 - 24
                visible: false
            }

            JQSettingsTextField {
                id: fileServerUri
                titleText: qsTr("文件服务URI")
                titleWidth: 170
                textFieldWidth: parent.width - 170 - 24
            }

            JQSettingsTextField {
                id: fileServerToken
                titleText: qsTr("文件服务Token")
                titleWidth: 170
                textFieldWidth: parent.width - 170 - 24
            }

            JQSettingsTextField {
                id: modelIdentifier
                titleText: qsTr("模型标识")
                titleWidth: 170
                textFieldWidth: parent.width - 170 - 24
                visible: false // 和skill绑定，暂不支持修改
            }
        }

        Row {
            anchors.right: parent.right

            JQButton {
                text: qsTr("保存配置")
                width: 120
                height: 50

                onClicked: {
                    if ( !nodeApplication )
                    {
                        JQGlobal.showMessage( qsTr("nodeApplication 未就绪") );
                        return;
                    }

                    nodeApplication.config = buildConfig();
                    if ( nodeApplication.saveConfig() )
                    {
                        JQGlobal.showMessage( qsTr("配置已保存") );
                    }
                    else
                    {
                        JQGlobal.showMessage( qsTr("保存失败，请查看日志") );
                    }
                }
            }
        }
    }
}
