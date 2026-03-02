import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import "./"
import "../"

JQSettingsRow {
    id: jqSettingsIpAddress

    property string address: defaultAddress
    property string defaultAddress: "0.0.0.0"
    property alias readOnly: jqIpField.readOnly
    property alias maximumLength: jqIpField.maximumLength
    property alias textFieldWidth: jqIpField.width
    property bool changingText: false

    function formatAddressText(rawText) {
        var segments = rawText.split( "." );
        var resultSegments = [];

        for ( var i = 0; i < segments.length && resultSegments.length < 4; i++ )
        {
            var digits = segments[i].replace( /[^0-9]/g, "" );

            while ( digits.length > 0 && resultSegments.length < 4 )
            {
                resultSegments.push( digits.slice( 0, 3 ) );
                digits = digits.slice( 3 );
            }
        }

        var result = resultSegments.join( "." );
        var trailingDot = rawText.length > 0 && rawText.charAt( rawText.length - 1 ) === ".";

        if ( trailingDot && resultSegments.length > 0 && resultSegments.length < 4 )
        {
            result += ".";
        }

        return result;
    }

    function normalizeAddress(value) {
        var parts = value.split( "." );
        var hasContent = false;
        var i;

        for ( i = 0; i < parts.length; i++ )
        {
            if ( parts[i] !== "" )
            {
                hasContent = true;
                break;
            }
        }

        if ( !hasContent )
        {
            return value;
        }

        while ( parts.length > 0 && parts[parts.length - 1] === "" )
        {
            parts.pop();
        }

        while ( parts.length < 4 )
        {
            parts.push( "0" );
        }

        if ( parts.length !== 4 )
        {
            return value;
        }

        for ( i = 0; i < parts.length; i++ )
        {
            if ( parts[i] === "" )
            {
                parts[i] = "0";
            }

            var segmentValue = parseInt( parts[i], 10 );
            if ( isNaN( segmentValue ) )
            {
                return value;
            }

            segmentValue = Math.max( 0, Math.min( 255, segmentValue ) );
            parts[i] = segmentValue.toString();
        }

        return parts.join( "." );
    }

    function syncAddressToText() {
        if ( changingText )
        {
            return;
        }

        var formatted = formatAddressText( address );
        if ( jqIpField.text === formatted && address === formatted )
        {
            return;
        }

        changingText = true;
        jqIpField.text = formatted;
        if ( address !== formatted )
        {
            address = formatted;
        }
        changingText = false;
    }

    onAddressChanged: {
        syncAddressToText();
    }

    Component.onCompleted: {
        syncAddressToText();
    }

    JQTextField {
        id: jqIpField
        anchors.verticalCenter: parent.verticalCenter
        inputMethodHints: Qt.ImhNoPredictiveText
        maximumLength: 15

        validator: RegularExpressionValidator {
            regularExpression: /^[0-9.]*$/
        }

        ToolTip.visible: hovered && enabled && ( jqSettingsIpAddress.tipText !== "" )
        ToolTip.text: jqSettingsIpAddress.tipText

        onTextChanged: {
            if ( jqSettingsIpAddress.changingText )
            {
                return;
            }

            jqSettingsIpAddress.changingText = true;

            var cursorPosition = jqIpField.cursorPosition;
            var formattedText = jqSettingsIpAddress.formatAddressText( text );
            var formattedCursorText = jqSettingsIpAddress.formatAddressText( text.slice( 0, cursorPosition ) );

            if ( formattedText !== text )
            {
                text = formattedText;
            }

            jqIpField.cursorPosition = Math.min( formattedCursorText.length, text.length );
            jqSettingsIpAddress.address = text;
            jqSettingsIpAddress.changingText = false;
        }

        onEditingFinished: {
            var normalized = jqSettingsIpAddress.normalizeAddress( text );
            if ( normalized !== text )
            {
                text = normalized;
            }
            jqSettingsIpAddress.address = text;
        }
    }
}


