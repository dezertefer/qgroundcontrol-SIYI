/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick                  2.12
import QtQuick.Controls         2.4
import QtQuick.Dialogs          1.3
import QtQuick.Layouts          1.12

import QtLocation               5.3
import QtPositioning            5.3
import QtQuick.Window           2.2
import QtQml.Models             2.1

import QGroundControl               1.0
import QGroundControl.Controls      1.0
import QGroundControl.Airspace      1.0
import QGroundControl.Airmap        1.0
import QGroundControl.Controllers   1.0
import QGroundControl.Controls      1.0
import QGroundControl.FactSystem    1.0
import QGroundControl.FlightDisplay 1.0
import QGroundControl.FlightMap     1.0
import QGroundControl.Palette       1.0
import QGroundControl.ScreenTools   1.0
import QGroundControl.Vehicle       1.0

// This is the ui overlay layer for the widgets/tools for Fly View
Item {
    id: _root

    property var    parentToolInsets
    property var    totalToolInsets:        _totalToolInsets
    property var    mapControl

    property var    _activeVehicle:         QGroundControl.multiVehicleManager.activeVehicle
    property var    _planMasterController:  globals.planMasterControllerFlyView
    property var    _missionController:     _planMasterController.missionController
    property var    _geoFenceController:    _planMasterController.geoFenceController
    property var    _rallyPointController:  _planMasterController.rallyPointController
    property var    _guidedController:      globals.guidedControllerFlyView
    property real   _margins:               ScreenTools.defaultFontPixelWidth / 2
    property real   _toolsMargin:           ScreenTools.defaultFontPixelWidth * 0.75
    property rect   _centerViewport:        Qt.rect(0, 0, width, height)
    property real   _rightPanelWidth:       ScreenTools.defaultFontPixelWidth * 30

    property real telemetryBarHeight: ScreenTools.defaultFontPixelHeight * 2
    property real telemetryBarMargin: _toolsMargin

    QGCPalette { id: qgcPal; colorGroupEnabled: true }

    QGCToolInsets {
        id:                     _totalToolInsets
        leftEdgeTopInset:       Math.max(toolStrip.leftInset, launchSpeedSlider.visible ? launchSpeedSlider.x + launchSpeedSlider.width : 0)
        leftEdgeCenterInset:    Math.max(toolStrip.leftInset, launchSpeedSlider.visible ? launchSpeedSlider.x + launchSpeedSlider.width : 0)
        leftEdgeBottomInset:    parentToolInsets.leftEdgeBottomInset
        rightEdgeTopInset:      parentToolInsets.rightEdgeTopInset
        rightEdgeCenterInset:   parentToolInsets.rightEdgeCenterInset
        rightEdgeBottomInset:   parentToolInsets.rightEdgeBottomInset
        topEdgeLeftInset:       parentToolInsets.topEdgeLeftInset
        topEdgeCenterInset:     parentToolInsets.topEdgeCenterInset
        topEdgeRightInset:      parentToolInsets.topEdgeRightInset
        bottomEdgeLeftInset:    parentToolInsets.bottomEdgeLeftInset
        bottomEdgeCenterInset:  Math.max(mapScale.centerInset, telemetryPanel.visible ? telemetryPanel.height + telemetryPanel.anchors.bottomMargin : 0)
        bottomEdgeRightInset:   0
    }

    FlyViewMissionCompleteDialog {
        missionController:      _missionController
        geoFenceController:     _geoFenceController
        rallyPointController:   _rallyPointController
    }

    Row {
        id:                 multiVehiclePanelSelector
        anchors.margins:    _toolsMargin
        anchors.top:        parent.top
        anchors.right:      parent.right
        width:              _rightPanelWidth
        spacing:            ScreenTools.defaultFontPixelWidth
        visible:            QGroundControl.multiVehicleManager.vehicles.count > 1 && QGroundControl.corePlugin.options.flyView.showMultiVehicleList

        property bool showSingleVehiclePanel:  !visible || singleVehicleRadio.checked

        QGCMapPalette { id: mapPal; lightColors: true }

        QGCRadioButton {
            id:             singleVehicleRadio
            text:           qsTr("Single")
            checked:        true
            textColor:      mapPal.text
        }

        QGCRadioButton {
            text:           qsTr("Multi-Vehicle")
            textColor:      mapPal.text
        }
    }

    MultiVehicleList {
        anchors.margins:    _toolsMargin
        anchors.top:        multiVehiclePanelSelector.bottom
        anchors.right:      parent.right
        width:              _rightPanelWidth
        height:             parent.height - y - _toolsMargin
        visible:            !multiVehiclePanelSelector.showSingleVehiclePanel
    }

    // FlyViewInstrumentPanel {
    //     id:                         instrumentPanel
    //     anchors.margins:            _toolsMargin
    //     anchors.top:                multiVehiclePanelSelector.visible ? multiVehiclePanelSelector.bottom : parent.top
    //     anchors.right:              parent.right
    //     width:                      _rightPanelWidth
    //     spacing:                    _toolsMargin
    //     visible:                    QGroundControl.corePlugin.options.flyView.showInstrumentPanel && multiVehiclePanelSelector.showSingleVehiclePanel
    //     availableHeight:            parent.height - y - _toolsMargin

    //     property real rightInset: visible ? parent.width - x : 0
    // }

    // PhotoVideoControl {
    //     id:                     photoVideoControl
    //     anchors.margins:        _toolsMargin
    //     anchors.right:          parent.right
    //     width:                  _rightPanelWidth
    //     state:                  _verticalCenter ? "verticalCenter" : "topAnchor"
    //     states: [
    //         State {
    //             name: "verticalCenter"
    //             AnchorChanges {
    //                 target:                 photoVideoControl
    //                 anchors.top:            undefined
    //                 anchors.verticalCenter: _root.verticalCenter
    //             }
    //         },
    //         State {
    //             name: "topAnchor"
    //             AnchorChanges {
    //                 target:                 photoVideoControl
    //                 anchors.verticalCenter: undefined
    //                 anchors.top:            instrumentPanel.bottom
    //             }
    //         }
    //     ]

    //     property bool _verticalCenter: !QGroundControl.settingsManager.flyViewSettings.alternateInstrumentPanel.rawValue
    // }

    TelemetryValuesBar {
        id:                     telemetryPanel
        anchors.left:           parent.left
        anchors.right:          parent.right
        anchors.bottom:         parent.bottom
        height:                 _root.height * 0.08//Math.max(ScreenTools.defaultFontPixelHeight * 3.0, _root.height * 0.08)
        z:                      QGroundControl.zOrderWidgets + 1
    }

    //-- Virtual Joystick
    Loader {
        id:                         virtualJoystickMultiTouch
        z:                          QGroundControl.zOrderTopMost + 1
        width:                      parent.width  - (_pipOverlay.width / 2)
        height:                     Math.min(parent.height * 0.25, ScreenTools.defaultFontPixelWidth * 16)
        visible:                    _virtualJoystickEnabled && !QGroundControl.videoManager.fullScreen && !(_activeVehicle ? _activeVehicle.usingHighLatencyLink : false)
        anchors.bottom:             parent.bottom
        anchors.bottomMargin:       parentToolInsets.leftEdgeBottomInset + ScreenTools.defaultFontPixelHeight * 2
        anchors.horizontalCenter:   parent.horizontalCenter
        source:                     "qrc:/qml/VirtualJoystick.qml"
        active:                     _virtualJoystickEnabled && !(_activeVehicle ? _activeVehicle.usingHighLatencyLink : false)

        property bool autoCenterThrottle: QGroundControl.settingsManager.appSettings.virtualJoystickAutoCenterThrottle.rawValue

        property bool _virtualJoystickEnabled: QGroundControl.settingsManager.appSettings.virtualJoystick.rawValue
    }

    FlyViewToolStrip {
        id:                     toolStrip
        anchors.leftMargin:     parentToolInsets.leftEdgeCenterInset
        anchors.topMargin:      parentToolInsets.topEdgeLeftInset
        anchors.left:           parent.left
        anchors.top:            parent.top
        z:                      QGroundControl.zOrderWidgets
        maxHeight:              (parent.height - y - parentToolInsets.bottomEdgeLeftInset) / scaleFactor
        visible:                !QGroundControl.videoManager.fullScreen

        // === added ===
        property real scaleFactor: 1.5
        transformOrigin: Item.TopLeft
        scale: scaleFactor
        // This is used by QGCToolInsets, so account for visual scale
        property real leftInset: x + width * scaleFactor
        // === end added ===

        onDisplayPreFlightChecklist: mainWindow.showPopupDialogFromComponent(preFlightChecklistPopup)

        //property real leftInset: x + width
    }

    Rectangle {
        id:                     launchSpeedSlider
        anchors.left:           parent.left
        anchors.leftMargin:     toolStrip.leftInset + _toolsMargin
        anchors.top:            parent.top
        anchors.topMargin:      _toolsMargin / 3 + parentToolInsets.topEdgeLeftInset
        anchors.bottom:         parent.bottom
        anchors.bottomMargin:   telemetryPanel.height + telemetryBarMargin + _toolsMargin
        width:                  ScreenTools.defaultFontPixelWidth * 8
        radius:                 ScreenTools.defaultFontPixelWidth / 2
        color:                  qgcPal.window
        border.color:           _phaseColor
        border.width:           3
        opacity:                0.92
        z:                      QGroundControl.zOrderTopMost
        visible:                !QGroundControl.videoManager.fullScreen &&
                                _missionController &&
                                _missionController.aerokontikiGuidedLaunchActive

        property bool _haulPhase:                       _missionController ? _missionController.aerokontikiGuidedLaunchHaulPhase : false
        property bool _speedTouchActive:                false
        property bool _speedTouchHaulPhase:             false
        property bool _speedTouchBlocked:               false
        property bool _syncingFromController:           false
        property color _phaseColor:                     _haulPhase ? qgcPal.brandingPurple : "#F28C28"
        property real _profileSpeedMetersPerSecond:     _missionController ? _missionController.aerokontikiGuidedLaunchProfileSpeed : 0
        property real _profileSpeedKph:                 Math.max(0.0, _profileSpeedMetersPerSecond * 3.6)
        property bool _profileAllowsSpeedBoost:         !_haulPhase && _profileSpeedKph <= 3.01
        property real _minSpeedKph:                     1
        property real _calculatedMaxSpeedKph:           Math.max(_minSpeedKph, Math.round(_profileSpeedKph * (_profileAllowsSpeedBoost ? 1.5 : 1.0) * 10) / 10)
        property real _calculatedSpeedStepKph:          _calculatedMaxSpeedKph <= 5 ? 0.5 : 1
        property real _maxSpeedKph:                     _calculatedMaxSpeedKph
        property real _speedStepKph:                    _calculatedSpeedStepKph
        property int  _speedStepCount:                  Math.max(0, Math.ceil((_maxSpeedKph - _minSpeedKph) / _speedStepKph))
        property int  _maxSpeedLabelCount:              10
        property int  _speedLabelCount:                 Math.min(_speedStepCount + 1, _maxSpeedLabelCount)

        function _speedForIndex(index) {
            return Math.min(_minSpeedKph + Math.max(0, index) * _speedStepKph, _maxSpeedKph)
        }

        function _stepIndexForLabel(labelIndex) {
            if (_speedLabelCount <= 1 || _speedStepCount === 0) {
                return 0
            }
            return Math.round((labelIndex / (_speedLabelCount - 1)) * _speedStepCount)
        }

        function _speedForLabel(labelIndex) {
            return _speedForIndex(_stepIndexForLabel(labelIndex))
        }

        function _currentSpeedKph() {
            return _missionController ?
                        Math.max(_minSpeedKph, Math.min(_maxSpeedKph, _missionController.aerokontikiGuidedLaunchSpeed * 3.6)) :
                        _minSpeedKph
        }

        function _formatSpeed(value) {
            return (_speedStepKph < 1 || Math.abs(value - Math.round(value)) > 0.01) ? value.toFixed(1) : value.toFixed(0)
        }

        function _labelY(labelIndex, labelHeight) {
            if (_speedStepCount === 0) {
                return Math.max(0, (speedSteps.height - labelHeight) / 2)
            }
            var stepIndex = _stepIndexForLabel(labelIndex)
            var rawY = speedSteps.height - ((stepIndex / _speedStepCount) * speedSteps.height) - labelHeight / 2
            return Math.max(0, Math.min(speedSteps.height - labelHeight, rawY))
        }

        function _syncFromController() {
            if (_missionController) {
                _syncingFromController = true
                speedSlider.value = Math.max(_minSpeedKph, Math.min(_maxSpeedKph, _missionController.aerokontikiGuidedLaunchSpeed * 3.6))
                _syncingFromController = false
            }
        }

        function _beginSpeedTouch() {
            if (_speedTouchActive) {
                return
            }

            _speedTouchActive = true
            _speedTouchHaulPhase = _haulPhase
            _speedTouchBlocked = false
        }

        function _speedTouchCanSetSpeed() {
            return _speedTouchActive &&
                    !_speedTouchBlocked &&
                    _speedTouchHaulPhase === _haulPhase
        }

        function _endSpeedTouch() {
            var canSetSpeed = _speedTouchCanSetSpeed()
            _speedTouchActive = false
            _speedTouchBlocked = false
            Qt.callLater(_syncFromController)
            return canSetSpeed
        }

        function _sendSpeed(speedKph) {
            if (!_missionController) {
                return
            }
            var boundedSpeedKph = Math.max(_minSpeedKph, Math.min(_maxSpeedKph, speedKph))
            console.info("AerokontikiSpeed qml-slider requestedKph=" + boundedSpeedKph +
                         " requestedMps=" + (boundedSpeedKph / 3.6) +
                         " haulPhase=" + _haulPhase)
            _missionController.setAerokontikiGuidedLaunchSpeed(boundedSpeedKph / 3.6)
        }

        onVisibleChanged: {
            _speedTouchActive = false
            _speedTouchBlocked = false
            if (visible) {
                _syncFromController()
            }
        }

        Connections {
            target: _missionController
            function onAerokontikiGuidedLaunchSpeedChanged() {
                if (launchSpeedSlider.visible && !launchSpeedSlider._speedTouchActive) {
                    launchSpeedSlider._syncFromController()
                }
            }
            function onAerokontikiGuidedLaunchHaulPhaseChanged() {
                if (launchSpeedSlider._speedTouchActive) {
                    launchSpeedSlider._speedTouchBlocked = true
                } else if (launchSpeedSlider.visible) {
                    launchSpeedSlider._syncFromController()
                }
            }
        }

        Column {
            anchors.fill:       parent
            anchors.margins:    ScreenTools.defaultFontPixelWidth * 0.6
            spacing:            ScreenTools.defaultFontPixelHeight * 0.25

            QGCLabel {
                anchors.left:           parent.left
                anchors.right:          parent.right
                horizontalAlignment:    Text.AlignHCenter
                wrapMode:               Text.WordWrap
                text:                   launchSpeedSlider._haulPhase ? qsTr("Haul Speed") : qsTr("Launch Speed")
                color:                  launchSpeedSlider._phaseColor
                font.bold:              true
                font.pointSize:         ScreenTools.smallFontPointSize
            }

            QGCLabel {
                anchors.left:           parent.left
                anchors.right:          parent.right
                horizontalAlignment:    Text.AlignHCenter
                text:                   launchSpeedSlider._formatSpeed(launchSpeedSlider._currentSpeedKph()) + qsTr(" km/h")
                color:                  qgcPal.text
                font.bold:              true
            }

            Item {
                anchors.left:           parent.left
                anchors.right:          parent.right
                height:                 parent.height - y

                QGCSlider {
                    id:                     speedSlider
                    anchors.left:           parent.left
                    anchors.right:          speedSteps.left
                    anchors.rightMargin:    ScreenTools.defaultFontPixelWidth * 0.4
                    anchors.top:            parent.top
                    anchors.bottom:         parent.bottom
                    orientation:            Qt.Vertical
                    minimumValue:           launchSpeedSlider._minSpeedKph
                    maximumValue:           launchSpeedSlider._maxSpeedKph
                    stepSize:               launchSpeedSlider._speedStepKph
                    tickmarksEnabled:       true
                    updateValueWhileDragging: true
                    displayValue:           false
                    indicatorColor:         launchSpeedSlider._phaseColor
                    rotation:               180

                    transform: Rotation {
                        origin.x:   speedSlider.width  / 2
                        origin.y:   speedSlider.height / 2
                        angle:      180
                    }

                    onPressedChanged: {
                        if (pressed) {
                            launchSpeedSlider._beginSpeedTouch()
                        } else if (launchSpeedSlider._speedTouchActive) {
                            launchSpeedSlider._endSpeedTouch()
                        }
                    }

                    onValueChanged: {
                        if (launchSpeedSlider.visible &&
                                !launchSpeedSlider._syncingFromController &&
                                launchSpeedSlider._speedTouchCanSetSpeed()) {
                            launchSpeedSlider._sendSpeed(value)
                        }
                    }
                }

                Item {
                    id:                     speedSteps
                    anchors.right:          parent.right
                    anchors.top:            parent.top
                    anchors.bottom:         parent.bottom
                    width:                  ScreenTools.defaultFontPixelWidth * 2.5

                    Repeater {
                        model: launchSpeedSlider._speedLabelCount

                        QGCLabel {
                            property real labelValue: launchSpeedSlider._speedForLabel(modelData)
                            x:                      0
                            y:                      launchSpeedSlider._labelY(modelData, height)
                            width:                  speedSteps.width
                            horizontalAlignment:    Text.AlignHCenter
                            text:                   launchSpeedSlider._formatSpeed(labelValue)
                            color:                  qgcPal.text
                            opacity:                1.0
                            font.pointSize:         ScreenTools.smallFontPointSize * 1.5
                        }
                    }

                    Repeater {
                        model: launchSpeedSlider._speedLabelCount

                        MouseArea {
                            x:          0
                            y:          launchSpeedSlider._labelY(modelData, height)
                            width:      speedSteps.width
                            height:     Math.max(ScreenTools.defaultFontPixelHeight, speedSteps.height / launchSpeedSlider._speedLabelCount)
                            onPressed: launchSpeedSlider._beginSpeedTouch()
                            onCanceled: {
                                if (launchSpeedSlider._speedTouchActive) {
                                    launchSpeedSlider._endSpeedTouch()
                                }
                            }
                            onReleased: {
                                var requestedSpeed = launchSpeedSlider._speedForLabel(modelData)
                                var canSetSpeed = launchSpeedSlider._endSpeedTouch()
                                if (canSetSpeed) {
                                    launchSpeedSlider._syncingFromController = true
                                    speedSlider.value = requestedSpeed
                                    launchSpeedSlider._syncingFromController = false
                                    launchSpeedSlider._sendSpeed(requestedSpeed)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    FlyViewAirspaceIndicator {
        anchors.top:                parent.top
        anchors.topMargin:          ScreenTools.defaultFontPixelHeight * 0.25
        anchors.horizontalCenter:   parent.horizontalCenter
        z:                          QGroundControl.zOrderWidgets
        show:                       mapControl.pipState.state !== mapControl.pipState.pipState
    }

    VehicleWarnings {
        anchors.centerIn:   parent
        z:                  QGroundControl.zOrderTopMost
    }

    MapScale {
        id:                 mapScale
        anchors.margins:    _toolsMargin
        // anchors.left:       toolStrip.right
        anchors.top:        parent.top
        anchors.right:      parent.right
        mapControl:         _mapControl
        buttonsOnLeft:      false
        visible:            !ScreenTools.isTinyScreen && QGroundControl.corePlugin.options.flyView.showMapScale && mapControl.pipState.state === mapControl.pipState.fullState

        property real centerInset: visible ? parent.height - y : 0
    }

    Component {
        id: preFlightChecklistPopup
        FlyViewPreFlightChecklistPopup {
        }
    }
}
