/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick          2.3
import QtQuick.Controls 1.2
import QtQuick.Layouts  1.2
import QtPositioning    5.3

import QGroundControl               1.0
import QGroundControl.ScreenTools   1.0
import QGroundControl.Controls      1.0
import QGroundControl.Palette       1.0

Item {
    id: root

    // Limit width so the background isn’t huge
    width:  ScreenTools.defaultFontPixelWidth * 22

    // Limit height so the panel never goes off-screen
    readonly property real _vMargin:   ScreenTools.defaultFontPixelHeight * 2
    readonly property real _maxHeight: mainWindow ? (mainWindow.height - _vMargin) : (ScreenTools.defaultFontPixelHeight * 25)
    height: Math.min(contentCol.implicitHeight + ScreenTools.defaultFontPixelHeight,
                     _maxHeight)

    property var    map
    property var    fitFunctions
    property bool   showMission:          true
    property bool   showAllItems:         true

    QGCFlickable {
        anchors.fill: parent
        contentWidth:  width
        contentHeight: contentCol.implicitHeight
        clip:          true

        ColumnLayout {
            id:         contentCol
            anchors.margins: ScreenTools.defaultFontPixelWidth
            anchors.left:    parent.left
            anchors.right:   parent.right
            spacing:    ScreenTools.defaultFontPixelWidth * 0.5

            QGCLabel { text: qsTr("Center map on:") }

            QGCButton {
                text:               qsTr("Mission")
                Layout.fillWidth:   true
                visible:            showMission
                onClicked: {
                    dropPanel.hide()
                    fitFunctions.fitMapViewportToMissionItems()
                }
            }

            QGCButton {
                text:               qsTr("All items")
                Layout.fillWidth:   true
                visible:            showAllItems
                onClicked: {
                    dropPanel.hide()
                    fitFunctions.fitMapViewportToAllItems()
                }
            }

            QGCButton {
                text:               qsTr("Launch")
                Layout.fillWidth:   true
                onClicked: {
                    dropPanel.hide()
                    map.center = fitFunctions.fitHomePosition()
                }
            }

            QGCButton {
                text:               qsTr("Vehicle")
                Layout.fillWidth:   true
                enabled:            globals.activeVehicle && globals.activeVehicle.coordinate.isValid
                onClicked: {
                    dropPanel.hide()
                    map.center = globals.activeVehicle.coordinate
                }
            }

            QGCButton {
                text:               qsTr("Current Location")
                Layout.fillWidth:   true
                enabled:            map.gcsPosition.isValid
                onClicked: {
                    dropPanel.hide()
                    map.center = map.gcsPosition
                }
            }

            QGCButton {
                text:               qsTr("Specified Location")
                Layout.fillWidth:   true
                onClicked: {
                    dropPanel.hide()
                    map.centerToSpecifiedLocation()
                }
            }
        }
    }
}
