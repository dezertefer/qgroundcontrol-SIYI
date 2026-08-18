/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick                      2.12
import QtQuick.Controls             2.12

import QGroundControl               1.0
import QGroundControl.Controls      1.0
import QGroundControl.ScreenTools   1.0

// Label control whichs pop up a flight mode change menu when clicked
QGCLabel {
    id:     _root
    text:   currentVehicle ? displayFlightMode(currentVehicle.flightMode) : qsTr("N/A", "No data to display")

    property var    currentVehicle:         QGroundControl.multiVehicleManager.activeVehicle
    property real   mouseAreaLeftMargin:    0

    Menu {
        id: flightModesMenu
    }

    Component {
        id: flightModeMenuItemComponent

        MenuItem {
            property string targetFlightMode
            enabled: true
            onTriggered: currentVehicle.flightMode = targetFlightMode
        }
    }

    property var flightModesMenuItems: []

    function _modeMatches(mode, targetMode, fallbackMode) {
        return mode &&
               ((targetMode && mode.toLowerCase() === targetMode.toLowerCase()) ||
                (fallbackMode && mode.toLowerCase() === fallbackMode.toLowerCase()))
    }

    function displayFlightMode(mode) {
        if (!currentVehicle) {
            return qsTr("N/A", "No data to display")
        }
        if (_modeMatches(mode, currentVehicle.takeControlFlightMode, "Loiter")) {
            return qsTr("Manual")
        }
        if (_modeMatches(mode, currentVehicle.gotoFlightMode, "Guided") ||
            _modeMatches(mode, currentVehicle.missionFlightMode, "Auto")) {
            return qsTr("Auto")
        }
        if (_modeMatches(mode, currentVehicle.rtlFlightMode, "RTL") ||
            _modeMatches(mode, currentVehicle.smartRTLFlightMode, "Smart RTL")) {
            return qsTr("RTL")
        }
        return mode
    }

    function _targetMode(preferredMode, fallbackMode) {
        return preferredMode && preferredMode.length ? preferredMode : fallbackMode
    }

    function _addFlightModeItem(label, targetMode) {
        var menuItem = flightModeMenuItemComponent.createObject(null, { "text": label, "targetFlightMode": targetMode })
        flightModesMenuItems.push(menuItem)
        flightModesMenu.insertItem(flightModesMenuItems.length - 1, menuItem)
    }

    function updateFlightModesMenu() {
        if (currentVehicle && currentVehicle.flightModeSetAvailable) {
            var i;
            // Remove old menu items
            for (i = 0; i < flightModesMenuItems.length; i++) {
                flightModesMenu.removeItem(flightModesMenuItems[i])
            }
            flightModesMenuItems.length = 0
            _addFlightModeItem(qsTr("Manual"), _targetMode(currentVehicle.takeControlFlightMode, "Loiter"))
            _addFlightModeItem(qsTr("Auto"),   _targetMode(currentVehicle.gotoFlightMode, "Guided"))
            _addFlightModeItem(qsTr("RTL"),    _targetMode(currentVehicle.rtlFlightMode, "RTL"))
        }
    }

    Component.onCompleted: _root.updateFlightModesMenu()

    Connections {
        target:                 QGroundControl.multiVehicleManager
        function onActiveVehicleChanged(activeVehicle) { _root.updateFlightModesMenu() }
    }

    MouseArea {
        id:                 mouseArea
        visible:            currentVehicle && currentVehicle.flightModeSetAvailable
        anchors.leftMargin: mouseAreaLeftMargin
        anchors.fill:       parent
        onClicked:          flightModesMenu.popup((_root.width - flightModesMenu.width) / 2, _root.height)
    }
}
