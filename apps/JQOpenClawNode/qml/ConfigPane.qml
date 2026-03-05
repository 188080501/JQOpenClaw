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

    function cloneConfig(configObject) {
        if ( !configObject )
        {
            return {};
        }

        try
        {
            return JSON.parse(JSON.stringify(configObject));
        }
        catch ( e )
        {
            return {};
        }
    }

    function applyConfig(configObject) {
        gatewayUrl.text = readString( configObject, "gatewayUrl", "ws://127.0.0.1:18789" );
        gatewayToken.text = readString( configObject, "token", "" );
        displayName.text = readString( configObject, "displayName", "" );
        instanceId.text = readString( configObject, "instanceId", "" );
        identityPath.text = readString( configObject, "identityPath", "" );
        fileServerUrl.text = readString( configObject, "fileServerUrl", "" );
        fileServerToken.text = readString( configObject, "fileServerToken", "" );
        modelIdentifier.text = readString( configObject, "modelIdentifier", "" );
    }

    function buildConfig() {
        var nextConfig = cloneConfig( nodeApplication ? nodeApplication.config : null );
        nextConfig.gatewayUrl = gatewayUrl.text.trim();
        nextConfig.token = gatewayToken.text;
        nextConfig.displayName = displayName.text.trim();
        nextConfig.identityPath = identityPath.text.trim();
        nextConfig.fileServerUrl = fileServerUrl.text.trim();
        nextConfig.fileServerToken = fileServerToken.text;
        nextConfig.modelIdentifier = modelIdentifier.text.trim();
        return nextConfig;
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
                id: gatewayUrl
                titleText: qsTr("网关URL")
                titleWidth: 170
                textFieldWidth: parent.width - 170 - 24
            }

            JQSettingsTextField {
                id: gatewayToken
                titleText: qsTr("网关Token")
                titleWidth: 170
                textFieldWidth: parent.width - 170 - 24
                echoMode: TextInput.Password
                copyEnabled: false
            }

            JQSettingsTextField {
                id: displayName
                titleText: qsTr("显示名称")
                titleWidth: 170
                textFieldWidth: parent.width - 170 - 24
            }

            JQSettingsTextField {
                id: instanceId
                titleText: qsTr("实例ID")
                titleWidth: 170
                textFieldWidth: parent.width - 170 - 24
                readOnly: true
                tipText: qsTr("实例ID在首次启动时自动生成，不允许手动修改")
            }

            JQSettingsTextField {
                id: identityPath
                titleText: qsTr("身份文件路径")
                titleWidth: 170
                textFieldWidth: parent.width - 170 - 24
                visible: false
            }

            JQSettingsTextField {
                id: fileServerUrl
                titleText: qsTr("文件服务URL")
                titleWidth: 170
                textFieldWidth: parent.width - 170 - 24
            }

            JQSettingsTextField {
                id: fileServerToken
                titleText: qsTr("文件服务Token")
                titleWidth: 170
                textFieldWidth: parent.width - 170 - 24
                echoMode: TextInput.Password
                copyEnabled: false
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
                text: qsTr("保存")
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
