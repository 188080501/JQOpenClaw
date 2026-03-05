import QtQuick
import JQControls

JQPane {
    id: permissionPane
    height: contentColumn.height + 40
    property bool applyingConfig: false

    function readBool(objectValue, key) {
        if ( objectValue && ( typeof objectValue[key] === "boolean" ) )
        {
            return objectValue[key];
        }
        return false;
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
        var permissionObject = {};
        if ( configObject &&
             configObject.permissions &&
             ( typeof configObject.permissions === "object" ) )
        {
            permissionObject = configObject.permissions;
        }

        applyingConfig = true;
        fileRead.checked = readBool(permissionObject, "file.read");
        fileWrite.checked = readBool(permissionObject, "file.write");
        processManage.checked = readBool(permissionObject, "process.manage");
        processExec.checked = readBool(permissionObject, "process.exec");
        processWhich.checked = readBool(permissionObject, "process.which");
        systemScreenshot.checked = readBool(permissionObject, "system.screenshot");
        systemInfo.checked = readBool(permissionObject, "system.info");
        systemNotify.checked = readBool(permissionObject, "system.notify");
        systemClipboard.checked = readBool(permissionObject, "system.clipboard");
        systemInput.checked = readBool(permissionObject, "system.input");
        nodeSelfUpdate.checked = readBool(permissionObject, "node.selfUpdate");
        applyingConfig = false;
    }

    function buildConfig() {
        var nextConfig = cloneConfig(nodeApplication ? nodeApplication.config : null);
        var permissionObject = {};
        if ( nextConfig.permissions && ( typeof nextConfig.permissions === "object" ) )
        {
            permissionObject = cloneConfig(nextConfig.permissions);
            if ( !permissionObject || ( typeof permissionObject !== "object" ) )
            {
                permissionObject = {};
            }
        }

        permissionObject["file.read"] = fileRead.checked;
        permissionObject["file.write"] = fileWrite.checked;
        permissionObject["process.manage"] = processManage.checked;
        permissionObject["process.exec"] = processExec.checked;
        permissionObject["process.which"] = processWhich.checked;
        permissionObject["system.screenshot"] = systemScreenshot.checked;
        permissionObject["system.info"] = systemInfo.checked;
        permissionObject["system.notify"] = systemNotify.checked;
        permissionObject["system.clipboard"] = systemClipboard.checked;
        permissionObject["system.input"] = systemInput.checked;
        permissionObject["node.selfUpdate"] = nodeSelfUpdate.checked;

        nextConfig.permissions = permissionObject;
        return nextConfig;
    }

    Component.onCompleted: {
        applyConfig(nodeApplication ? nodeApplication.config : null);
    }

    Connections {
        target: nodeApplication

        function onConfigChanged(config) {
            applyConfig(config);
        }
    }

    Column {
        id: contentColumn
        x: 12
        y: 12
        width: parent.width - 24
        spacing: 8

        Text {
            text: qsTr("能力权限")
            font.pixelSize: 20
            font.bold: true
            color: "#111827"
        }

        JQSettingsCheckBox {
            id: fileRead
            titleText: qsTr("file.read")
            titleWidth: 170
            tipText: qsTr("支持 operation=read/lines/list/rg/stat/md5：文件读取、按行读取、目录遍历、元信息查询与文件 MD5 计算。")
        }

        JQSettingsCheckBox {
            id: fileWrite
            titleText: qsTr("file.write")
            titleWidth: 170
            tipText: qsTr("支持写入/移动/删除/目录创建/目录删除：operation=write/move/delete/mkdir/rmdir，并支持 createDirs/overwrite。")
        }

        JQSettingsCheckBox {
            id: processManage
            titleText: qsTr("process.manage")
            titleWidth: 170
            tipText: qsTr("进程管理：operation=list/search/kill，可列出进程、按关键字或 PID 搜索，并按 PID 终止进程。")
        }

        JQSettingsCheckBox {
            id: processExec
            titleText: qsTr("process.exec")
            titleWidth: 170
            tipText: qsTr("基于 QProcess 远程执行进程命令，返回 exitCode/stdout/stderr 等结果。")
        }

        JQSettingsCheckBox {
            id: processWhich
            titleText: qsTr("process.which")
            titleWidth: 170
            tipText: qsTr("探测命令是否可执行并返回可执行路径，支持 program 或 programs 批量查询。")
        }

        JQSettingsCheckBox {
            id: systemScreenshot
            titleText: qsTr("system.screenshot")
            titleWidth: 170
            tipText: qsTr("采集桌面截图并返回图片信息（JPG）。")
        }

        JQSettingsCheckBox {
            id: systemInfo
            titleText: qsTr("system.info")
            titleWidth: 170
            tipText: qsTr("采集系统基础信息：CPU、主机名、系统版本、用户名、内存、GPU、IP、硬盘容量等。")
        }

        JQSettingsCheckBox {
            id: systemNotify
            titleText: qsTr("system.notify")
            titleWidth: 170
            tipText: qsTr("系统弹窗能力：弹出消息提示框（支持 message，title 可选）。")
        }

        JQSettingsCheckBox {
            id: systemClipboard
            titleText: qsTr("system.clipboard")
            titleWidth: 170
            tipText: qsTr("系统剪贴板能力：读取当前剪贴板文本，或写入文本到剪贴板。")
        }

        JQSettingsCheckBox {
            id: systemInput
            titleText: qsTr("system.input")
            titleWidth: 170
            tipText: qsTr("输入控制动作列表：mouse.move/click/scroll/drag、keyboard.down/up/tap/text、delay；异步入队且 latest-wins。")
        }

        JQSettingsCheckBox {
            id: nodeSelfUpdate
            titleText: qsTr("node.selfUpdate")
            titleWidth: 170
            tipText: qsTr("节点自更新能力：下载更新包并启动替换脚本，回包后自动退出当前进程。")
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
                        JQGlobal.showMessage(qsTr("nodeApplication 未就绪"));
                        return;
                    }

                    nodeApplication.config = buildConfig();
                    if ( nodeApplication.saveConfig() )
                    {
                        JQGlobal.showMessage(qsTr("权限已保存，正在重连网关"));
                    }
                    else
                    {
                        JQGlobal.showMessage(qsTr("保存失败，请查看日志"));
                    }
                }
            }
        }
    }
}
