/****************************************************************************
 *
 *   Simplified fixed telemetry panel
 *   Plain QML only
 *
 ****************************************************************************/

import QtQuick                      2.12
import QtQuick.Layouts              1.12

import QGroundControl               1.0
import QGroundControl.ScreenTools   1.0
import QGroundControl.Controls      1.0
import QGroundControl.Palette       1.0

Rectangle {
    id: telemetryPanel

    property var activeVehicle: QGroundControl.multiVehicleManager.activeVehicle

    // Compact one-line telemetry font sizes
    property real labelPixelSize: ScreenTools.defaultFontPixelHeight * 0.60
    property real valuePixelSize: ScreenTools.defaultFontPixelHeight * 0.85

    // -----------------------------------------------------------------
    // Fixed slot widths.
    // These prevent fields from jumping when values gain/lose digits.
    // Tune these if a specific value gets clipped.
    // -----------------------------------------------------------------
    property real voltageSlotWidth:  ScreenTools.defaultFontPixelWidth * 10
    property real altitudeSlotWidth: ScreenTools.defaultFontPixelWidth * 10.5
    property real usedSlotWidth:     ScreenTools.defaultFontPixelWidth * 15.0
    property real speedSlotWidth:    ScreenTools.defaultFontPixelWidth * 13.5
    property real wattsSlotWidth:    ScreenTools.defaultFontPixelWidth * 9.5
    property real homeSlotWidth:     ScreenTools.defaultFontPixelWidth * 12.0

    // -----------------------------------------------------------------
    // Gaps between fixed slots.
    // -----------------------------------------------------------------
    property real leftContentMargin: 4
    property real rightContentMargin: 4
    property real labelValueGap:     2

    // Requested spacing
    property real startToVoltageGap: 15
    property real voltageToAltGap:   10
    property real normalGroupGap:    10
    property real usedToSpeedGap:    12        // 20% bigger than 10
    property real wattsToHomeGap:    6.5       // 35% smaller than 10

    radius: ScreenTools.defaultFontPixelWidth * 0.5
    color: qgcPal.window
    border.width: 1
    border.color: qgcPal.windowShade

    QGCPalette {
        id: qgcPal
        colorGroupEnabled: enabled
    }

    DeadMouseArea {
        anchors.fill: parent
    }

    function fmt(v, digits) {
        if (v === undefined || v === null || isNaN(v))
            return "--"
        return Number(v).toFixed(digits)
    }

    function batteryGroup() {
        if (!activeVehicle || !activeVehicle.batteries || activeVehicle.batteries.count === 0)
            return null
        return activeVehicle.batteries.get(0)
    }

    // -----------------------------------------------------------------
    // Unit helpers
    //
    // QGC speed units:
    // 0 = ft/s
    // 1 = m/s
    // 2 = mph
    // 3 = km/h
    // 4 = knots
    // -----------------------------------------------------------------

    function speedUnitsRaw() {
        if (!QGroundControl.settingsManager ||
                !QGroundControl.settingsManager.unitsSettings ||
                !QGroundControl.settingsManager.unitsSettings.speedUnits) {
            return 1
        }

        return Number(QGroundControl.settingsManager.unitsSettings.speedUnits.rawValue)
    }

    function speedUnitsString() {
        switch (speedUnitsRaw()) {
        case 0:
            return "ft/s"
        case 1:
            return "m/s"
        case 2:
            return "mph"
        case 3:
            return "km/h"
        case 4:
            return "kn"
        }

        return "m/s"
    }

    function metersSecondToSelectedSpeed(ms) {
        switch (speedUnitsRaw()) {
        case 0:
            return ms * 3.280839895
        case 1:
            return ms
        case 2:
            return ms * 2.236936292
        case 3:
            return ms * 3.6
        case 4:
            return ms * 1.943844492
        }

        return ms
    }

    // -----------------------------------------------------------------
    // Text helpers
    // -----------------------------------------------------------------

    function voltageText() {
        var b = batteryGroup()
        if (!b || !b.voltage)
            return "--"

        return fmt(b.voltage.rawValue, 1) + "V"
    }

    function relativeAltitudeText() {
        if (!activeVehicle || !activeVehicle.altitudeRelative)
            return "--"

        return fmt(activeVehicle.altitudeRelative.rawValue, 1) + "m"
    }

    function consumedMahText() {
        var b = batteryGroup()
        if (!b || !b.mahConsumed)
            return "--"

        var mah = Number(b.mahConsumed.rawValue)
        if (isNaN(mah))
            return "--"

        // Save space:
        // 12400mAh -> 12.4Ah
        if (mah >= 1000)
            return (mah / 1000.0).toFixed(1) + "Ah"

        return mah.toFixed(0) + "mAh"
    }

    function groundSpeedText() {
        if (!activeVehicle || !activeVehicle.groundSpeed)
            return "--"

        var ms = Number(activeVehicle.groundSpeed.rawValue)
        if (isNaN(ms))
            return "--"

        return fmt(metersSecondToSelectedSpeed(ms), 1) + speedUnitsString()
    }

    function wattsText() {
        var b = batteryGroup()
        if (!b)
            return "--"

        if (b.instantPower)
            return fmt(b.instantPower.rawValue, 0) + "W"

        if (!b.voltage || !b.current)
            return "--"

        var v = Number(b.voltage.rawValue)
        var a = Number(b.current.rawValue)

        if (isNaN(v) || isNaN(a))
            return "--"

        return fmt(v * a, 0) + "W"
    }

    function distanceToHomeText() {
        if (!activeVehicle || !activeVehicle.distanceToHome)
            return "--"

        return fmt(activeVehicle.distanceToHome.rawValue, 0) + "m"
    }

    RowLayout {
        anchors.fill: parent

        anchors.leftMargin:     telemetryPanel.leftContentMargin
        anchors.rightMargin:    telemetryPanel.rightContentMargin
        anchors.topMargin:      ScreenTools.defaultFontPixelHeight * 0.08
        anchors.bottomMargin:   ScreenTools.defaultFontPixelHeight * 0.08

        spacing: 0

        // -----------------------------------------------------------------
        // Voltage value only
        // Fixed-width slot, no label.
        // -----------------------------------------------------------------
        // -----------------------------------------------------------------
        // Battery voltage
        // -----------------------------------------------------------------
        Item {
            Layout.preferredWidth: telemetryPanel.startToVoltageGap
            Layout.fillHeight: true
        }

        Item {
            Layout.preferredWidth: telemetryPanel.voltageSlotWidth
            Layout.fillHeight: true

            RowLayout {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: telemetryPanel.labelValueGap

                QGCLabel {
                    text: "Batt"
                    font.pixelSize: telemetryPanel.labelPixelSize
                    color: qgcPal.text
                    opacity: 0.7
                    verticalAlignment: Text.AlignVCenter
                    Layout.alignment: Qt.AlignVCenter
                }

                QGCLabel {
                    text: telemetryPanel.voltageText()
                    font.pixelSize: telemetryPanel.valuePixelSize
                    font.bold: true
                    color: qgcPal.text
                    horizontalAlignment: Text.AlignLeft
                    verticalAlignment: Text.AlignVCenter
                    Layout.alignment: Qt.AlignVCenter
                }
            }
        }

        Item {
            Layout.preferredWidth: telemetryPanel.voltageToAltGap
            Layout.fillHeight: true
        }

        // -----------------------------------------------------------------
        // Altitude
        // -----------------------------------------------------------------
        Item {
            Layout.preferredWidth: telemetryPanel.altitudeSlotWidth
            Layout.fillHeight: true

            RowLayout {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: telemetryPanel.labelValueGap

                QGCLabel {
                    text: "Alt"
                    font.pixelSize: telemetryPanel.labelPixelSize
                    color: qgcPal.text
                    opacity: 0.7
                    verticalAlignment: Text.AlignVCenter
                    Layout.alignment: Qt.AlignVCenter
                }

                QGCLabel {
                    text: telemetryPanel.relativeAltitudeText()
                    font.pixelSize: telemetryPanel.valuePixelSize
                    font.bold: true
                    color: qgcPal.text
                    horizontalAlignment: Text.AlignLeft
                    verticalAlignment: Text.AlignVCenter
                    Layout.alignment: Qt.AlignVCenter
                }
            }
        }

        Item {
            Layout.preferredWidth: telemetryPanel.normalGroupGap
            Layout.fillHeight: true
        }

        // -----------------------------------------------------------------
        // Used / consumed capacity
        // -----------------------------------------------------------------
        Item {
            Layout.preferredWidth: telemetryPanel.usedSlotWidth
            Layout.fillHeight: true

            RowLayout {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: telemetryPanel.labelValueGap

                QGCLabel {
                    text: "Used"
                    font.pixelSize: telemetryPanel.labelPixelSize
                    color: qgcPal.text
                    opacity: 0.7
                    verticalAlignment: Text.AlignVCenter
                    Layout.alignment: Qt.AlignVCenter
                }

                QGCLabel {
                    text: telemetryPanel.consumedMahText()
                    font.pixelSize: telemetryPanel.valuePixelSize
                    font.bold: true
                    color: qgcPal.text
                    horizontalAlignment: Text.AlignLeft
                    verticalAlignment: Text.AlignVCenter
                    Layout.alignment: Qt.AlignVCenter
                }
            }
        }

        Item {
            Layout.preferredWidth: telemetryPanel.usedToSpeedGap
            Layout.fillHeight: true
        }

        // -----------------------------------------------------------------
        // Ground speed
        // -----------------------------------------------------------------
        Item {
            Layout.preferredWidth: telemetryPanel.speedSlotWidth
            Layout.fillHeight: true

            RowLayout {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: telemetryPanel.labelValueGap

                QGCLabel {
                    text: "Spd"
                    font.pixelSize: telemetryPanel.labelPixelSize
                    color: qgcPal.text
                    opacity: 0.7
                    verticalAlignment: Text.AlignVCenter
                    Layout.alignment: Qt.AlignVCenter
                }

                QGCLabel {
                    text: telemetryPanel.groundSpeedText()
                    font.pixelSize: telemetryPanel.valuePixelSize
                    font.bold: true
                    color: qgcPal.text
                    horizontalAlignment: Text.AlignLeft
                    verticalAlignment: Text.AlignVCenter
                    Layout.alignment: Qt.AlignVCenter
                }
            }
        }

        Item {
            Layout.preferredWidth: telemetryPanel.normalGroupGap
            Layout.fillHeight: true
        }

        // -----------------------------------------------------------------
        // Watts
        // -----------------------------------------------------------------
        Item {
            Layout.preferredWidth: telemetryPanel.wattsSlotWidth
            Layout.fillHeight: true

            RowLayout {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: telemetryPanel.labelValueGap

                QGCLabel {
                    text: "Pwr"
                    font.pixelSize: telemetryPanel.labelPixelSize
                    color: qgcPal.text
                    opacity: 0.7
                    verticalAlignment: Text.AlignVCenter
                    Layout.alignment: Qt.AlignVCenter
                }

                QGCLabel {
                    text: telemetryPanel.wattsText()
                    font.pixelSize: telemetryPanel.valuePixelSize
                    font.bold: true
                    color: qgcPal.text
                    horizontalAlignment: Text.AlignLeft
                    verticalAlignment: Text.AlignVCenter
                    Layout.alignment: Qt.AlignVCenter
                }
            }
        }

        Item {
            Layout.preferredWidth: telemetryPanel.wattsToHomeGap
            Layout.fillHeight: true
        }

        // -----------------------------------------------------------------
        // Distance home
        // -----------------------------------------------------------------
        Item {
            Layout.preferredWidth: telemetryPanel.homeSlotWidth
            Layout.fillHeight: true

            RowLayout {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: telemetryPanel.labelValueGap

                QGCLabel {
                    text: "Home"
                    font.pixelSize: telemetryPanel.labelPixelSize
                    color: qgcPal.text
                    opacity: 0.7
                    verticalAlignment: Text.AlignVCenter
                    Layout.alignment: Qt.AlignVCenter
                }

                QGCLabel {
                    text: telemetryPanel.distanceToHomeText()
                    font.pixelSize: telemetryPanel.valuePixelSize
                    font.bold: true
                    color: qgcPal.text
                    horizontalAlignment: Text.AlignLeft
                    verticalAlignment: Text.AlignVCenter
                    Layout.alignment: Qt.AlignVCenter
                }
            }
        }

        // Remaining space stays empty on the right.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
