/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick          2.15
import QtQuick.Controls 1.2
import QtQuick.Dialogs  1.2
import QtLocation       5.15
import QtPositioning    5.15
import QtQuick.Layouts  1.2
import QtQuick.Window   2.2
import QtGraphicalEffects 1.12

import QGroundControl                   1.0
import QGroundControl.FlightMap         1.0
import QGroundControl.ScreenTools       1.0
import QGroundControl.Controls          1.0
import QGroundControl.FactSystem        1.0
import QGroundControl.FactControls      1.0
import QGroundControl.Palette           1.0
import QGroundControl.Controllers       1.0
import QGroundControl.ShapeFileHelper   1.0
import QGroundControl.Airspace          1.0
import QGroundControl.Airmap            1.0

import io.qt.examples.backend           1.0


Item {
    id: _root

    property bool planControlColapsed: false

    property bool dropPointSelected: QGroundControl.settingsManager.planViewSettings.dropPointSelected.rawValue

    readonly property int   _decimalPlaces:             8
    readonly property real  _margin:                    ScreenTools.defaultFontPixelHeight * 0.5
    readonly property real  _toolsMargin:               ScreenTools.defaultFontPixelWidth * 0.75
    readonly property real  _radius:                    ScreenTools.defaultFontPixelWidth  * 0.5
    readonly property real  _rightPanelWidth:           Math.min(parent.width / 3, ScreenTools.defaultFontPixelWidth * 30)
    readonly property var   _defaultVehicleCoordinate:  QtPositioning.coordinate(37.803784, -122.462276)
    readonly property bool  _waypointsOnlyMode:         QGroundControl.corePlugin.options.missionWaypointsOnly

    property bool   _airspaceEnabled:                    QGroundControl.airmapSupported ? (QGroundControl.settingsManager.airMapSettings.enableAirMap.rawValue && QGroundControl.airspaceManager.connected): false
    property var    _missionController:                 _planMasterController.missionController
    property var    _geoFenceController:                _planMasterController.geoFenceController
    property var    _rallyPointController:              _planMasterController.rallyPointController
    property var    _visualItems:                       _missionController.visualItems
    property bool   _lightWidgetBorders:                editorMap.isSatelliteMap
    property bool   _addROIOnClick:                     false
    property bool   _singleComplexItem:                 _missionController.complexMissionItemNames.length === 1
    property int    _editingLayer:                      layerTabBar.currentIndex ? _layers[layerTabBar.currentIndex] : _layerMission
    property int    _toolStripBottom:                   toolStrip.height + toolStrip.y
    property var    _appSettings:                       QGroundControl.settingsManager.appSettings
    property var    _planViewSettings:                  QGroundControl.settingsManager.planViewSettings
    property bool   _promptForPlanUsageShowing:         false
    property real   _valueFieldWidth:                   ScreenTools.defaultFontPixelWidth * 9
    property bool   _isEdit:                            false
    property bool   _isNew:                             false
    property bool   _isDel:                             false
    property var    _newDropPointCoord:                 QtPositioning.coordinate()
    //property var    pointToAdd: {"lat": 0 , "lon":0}

    property var    historyItemData

    readonly property var       _layers:                [_layerMission, _layerGeoFence, _layerRallyPoints]

    readonly property int       _layerMission:              1
    readonly property int       _layerGeoFence:             2
    readonly property int       _layerRallyPoints:          3
    property var _pendingCoord: null

    property bool enableDebug: false

    property real _takeoffRel: 2.0

    function _minHaulAltRel() {
        var angleDeg = Number(_planViewSettings.currentProfileAngle.rawValue) || 0
        var L        = Number(_planViewSettings.currentProfileCableLength.rawValue) || 0
        var theta    = angleDeg * Math.PI / 180.0
        return _takeoffRel + Math.max(0, L * Math.sin(theta)) + 10
    }

    // Clamp the Fact if it's too low
    function _enforceHaulAltitudeMin(showToast) {
        var minAlt = _minHaulAltRel()
        var curAlt = Number(_planViewSettings.currentProfileAlt.rawValue) || 0
        if (curAlt < minAlt) {
            _planViewSettings.currentProfileAlt.rawValue = minAlt
            if (showToast) {
                mainWindow.showMessageDialog(
                    qsTr("Profile constraint"),
                    qsTr("Haul altitude was raised to %1 m (minimum for %2 m cable @ %3°).")
                        .arg(minAlt.toFixed(1))
                        .arg(Number(_planViewSettings.currentProfileCableLength.rawValue).toFixed(1))
                        .arg(Number(_planViewSettings.currentProfileAngle.rawValue).toFixed(0))
                )
            }
        }
    }

    Connections {
        target: _planViewSettings.currentProfileAlt
        function onRawValueChanged() { _enforceHaulAltitudeMin(false) }
    }
    Connections {
        target: _planViewSettings.currentProfileAngle
        function onRawValueChanged() { _enforceHaulAltitudeMin(true) }
    }
    Connections {
        target: _planViewSettings.currentProfileCableLength
        function onRawValueChanged() { _enforceHaulAltitudeMin(true) }
    }
    Component.onCompleted: _enforceHaulAltitudeMin(false)

    Timer {
        id: rebuildPollTimer
        interval: 300
        repeat: true
        running: false
        onTriggered: {
            // Rebuild once the model is truly empty (some builds keep a home visual, so <= 1)
            if (!_missionController.containsItems || _missionController.visualItems.count <= 1) {
                rebuildPollTimer.stop()
                if (_pendingCoord) {
                    backend.dropPointSelected = false
                    insertSimpleItemAfterCurrent(_pendingCoord)   // your existing builder, unchanged
                    _pendingCoord = null
                }
            }
        }
    }

    Connections {
        target: globals
        function onDragActiveChanged() {
            // Only act when drag just ended
            if (!globals.dragActive && globals.dragCoordinate) {
                _pendingCoord = globals.dragCoordinate
                // Clear asynchronously
                _planMasterController.removeAllFromVehicle()
                _missionController.setCurrentPlanViewSeqNum(0, true)
                // Start polling until empty, then rebuild
                rebuildPollTimer.start()
            }
        }
    }

    function mapCenter() {
        var coordinate = editorMap.center
        coordinate.latitude  = coordinate.latitude.toFixed(_decimalPlaces)
        coordinate.longitude = coordinate.longitude.toFixed(_decimalPlaces)
        coordinate.altitude  = coordinate.altitude.toFixed(_decimalPlaces)
        return coordinate
    }

    function updateAirspace(reset) {
        if(_airspaceEnabled) {
            var coordinateNW = editorMap.toCoordinate(Qt.point(0,0), false /* clipToViewPort */)
            var coordinateSE = editorMap.toCoordinate(Qt.point(width,height), false /* clipToViewPort */)
            if(coordinateNW.isValid && coordinateSE.isValid) {
                QGroundControl.airspaceManager.setROI(coordinateNW, coordinateSE, true /*planView*/, reset)
            }
        }
    }

    property bool _firstMissionLoadComplete:    false
    property bool _firstFenceLoadComplete:      false
    property bool _firstRallyLoadComplete:      false
    property bool _firstLoadComplete:           false

    MapFitFunctions {
        id:                         mapFitFunctions  // The name for this id cannot be changed without breaking references outside of this code. Beware!
        map:                        editorMap
        usePlannedHomePosition:     true
        planMasterController:       _planMasterController
    }

    on_AirspaceEnabledChanged: {
        if(QGroundControl.airmapSupported) {
            if(_airspaceEnabled) {
                planControlColapsed = QGroundControl.airspaceManager.airspaceVisible
                updateAirspace(true)
            } else {
                planControlColapsed = false
            }
        } else {
            planControlColapsed = false
        }
    }

    onVisibleChanged: {
        if(visible) {
            editorMap.zoomLevel = QGroundControl.flightMapZoom
            editorMap.center    = QGroundControl.flightMapPosition
            if (!_planMasterController.containsItems) {
                toolStrip.simulateClick(toolStrip.fileButtonIndex)
            }
        }
    }

    Connections {
        target: _appSettings ? _appSettings.defaultMissionItemAltitude : null
        function onRawValueChanged() {
            if (_visualItems.count > 1) {
                mainWindow.showComponentDialog(applyNewAltitude, qsTr("Apply new altitude"), mainWindow.showDialogDefaultWidth, StandardButton.Yes | StandardButton.No)
            }
        }
    }

    Component {
        id: applyNewAltitude
        QGCViewMessage {
            message:    qsTr("You have changed the default altitude for mission items. Would you like to apply that altitude to all the items in the current mission?")
            function accept() {
                hideDialog()
                _missionController.applyDefaultMissionAltitude()
            }
        }
    }

    Component {
        id: promptForPlanUsageOnVehicleChangePopupComponent
        QGCPopupDialog {
            title:      _planMasterController.managerVehicle.isOfflineEditingVehicle ? qsTr("Plan View - Vehicle Disconnected") : qsTr("Plan View - Vehicle Changed")
            buttons:    StandardButton.NoButton

            ColumnLayout {
                QGCLabel {
                    Layout.maximumWidth:    parent.width
                    wrapMode:               QGCLabel.WordWrap
                    text:                   _planMasterController.managerVehicle.isOfflineEditingVehicle ?
                                                qsTr("The vehicle associated with the plan in the Plan View is no longer available. What would you like to do with that plan?") :
                                                qsTr("The plan being worked on in the Plan View is not from the current vehicle. What would you like to do with that plan?")
                }

                QGCButton {
                    Layout.fillWidth:   true
                    text:               _planMasterController.dirty ?
                                            (_planMasterController.managerVehicle.isOfflineEditingVehicle ?
                                                 qsTr("Discard Unsaved Changes") :
                                                 qsTr("Discard Unsaved Changes, Load New Plan From Vehicle")) :
                                            qsTr("Load New Plan From Vehicle")
                    onClicked: {
                        _planMasterController.showPlanFromManagerVehicle()
                        _promptForPlanUsageShowing = false
                        hideDialog();
                    }
                }

                QGCButton {
                    Layout.fillWidth:   true
                    text:               _planMasterController.managerVehicle.isOfflineEditingVehicle ?
                                            qsTr("Keep Current Plan") :
                                            qsTr("Keep Current Plan, Don't Update From Vehicle")
                    onClicked: {
                        if (!_planMasterController.managerVehicle.isOfflineEditingVehicle) {
                            _planMasterController.dirty = true
                        }
                        _promptForPlanUsageShowing = false
                        hideDialog()
                    }
                }
            }
        }
    }


    Connections {
        target: QGroundControl.airspaceManager
        function onAirspaceVisibleChanged() {
            planControlColapsed = QGroundControl.airspaceManager.airspaceVisible
        }
    }

    Component {
        id: noItemForKML
        QGCViewMessage {
            message:    qsTr("You need at least one item to create a KML.")
        }
    }



    PlanMasterController {
        id:                         _planMasterController
        flyView:                    false
        aerokontikiMissionStorage:  true

        Component.onCompleted: {
            _planMasterController.start()
            _missionController.setCurrentPlanViewSeqNum(0, true)
            globals.planMasterControllerPlanView = _planMasterController
        }

        onPromptForPlanUsageOnVehicleChange: {
            if (!_promptForPlanUsageShowing) {
                _promptForPlanUsageShowing = true
                mainWindow.showPopupDialogFromComponent(promptForPlanUsageOnVehicleChangePopupComponent)
            }
        }

        function waitingOnIncompleteDataMessage() {
            mainWindow.showMessageDialog(qsTr("Unable to Save"), qsTr("Plan has incomplete items. Complete all items and save again."))
        }

        function waitingOnTerrainDataMessage() {
            mainWindow.showMessageDialog(qsTr("Unable to Save"), qsTr("Plan is waiting on terrain data from server for correct altitude values."))
        }

        function checkReadyForSave() {
            if (readyForSaveState() == VisualMissionItem.NotReadyForSaveData) {
                waitingOnIncompleteDataMessage()
                return false
            } else if (readyForSaveState() == VisualMissionItem.NotReadyForSaveTerrain) {
                //waitingOnTerrainDataMessage(save)
                //return false
            }
            return true
        }

        function loadFromSelectedFile() {
            fileDialog.title =          qsTr("Select Plan File")
            fileDialog.planFiles =      true
            fileDialog.selectExisting = true
            fileDialog.nameFilters =    _planMasterController.loadNameFilters
            fileDialog.openForLoad()
        }

        function saveToSelectedFile() {
            if (!checkReadyForSave()) {
                return
            }
            fileDialog.title =          qsTr("Save Plan")
            fileDialog.planFiles =      true
            fileDialog.selectExisting = false
            fileDialog.nameFilters =    _planMasterController.saveNameFilters
            fileDialog.openForSave()
        }

        function fitViewportToItems() {
            mapFitFunctions.fitMapViewportToMissionItems()
        }

        function saveKmlToSelectedFile() {
            if (!checkReadyForSave()) {
                return
            }
            fileDialog.title =          qsTr("Save KML")
            fileDialog.planFiles =      false
            fileDialog.selectExisting = false
            fileDialog.nameFilters =    ShapeFileHelper.fileDialogKMLFilters
            fileDialog.openForSave()
        }
    }

    Connections {
        target: _missionController

        function onNewItemsFromVehicle() {
            if (_visualItems && _visualItems.count !== 1) {
                mapFitFunctions.fitMapViewportToMissionItems()
            }
            _missionController.setCurrentPlanViewSeqNum(0, true)
        }

    }

    /*BackEnd {
           id: backend
       }*/
    function openHistory(){

    }

    // function insertSimpleItemAfterCurrent(targetCoord) {
    //     // Clear existing plan if anything is already there
    //     if (_missionController.containsItems) {
    //         _planMasterController.removeAllFromVehicle()
    //         _missionController.setCurrentPlanViewSeqNum(0, true)
    //     }

    //     // A = current vehicle position, B = user click
    //     const A = globals.activeVehicle.coordinate
    //     backend.A = A
    //     backend.B = targetCoord

    //     // Force backend to compute C/D (side-effect inside getter)
    //     void backend.angle

    //     // Convert absolute (MSL) -> relative (home) for mission items
    //     function toRelative(c) {
    //         const home = globals.activeVehicle.homePosition   // MSL
    //         return QtPositioning.coordinate(c.latitude, c.longitude, c.altitude - home.altitude)
    //     }

    //     let idx = 1

    //     // 1) Takeoff at A (QGC uses relative alt by default for simple items)
    //     _missionController.insertTakeoffItem(A, idx++, true)

    //     // 2) Climb/translate to C
    //     _missionController.insertSimpleMissionItem(toRelative(backend.C), idx++, true)

    //     // 3) If cable-limited, go to D (skip if C==D)
    //     if (backend.D.latitude  !== backend.C.latitude  ||
    //         backend.D.longitude !== backend.C.longitude ||
    //         backend.D.altitude  !== backend.C.altitude) {
    //         _missionController.insertSimpleMissionItem(toRelative(backend.D), idx++, true)
    //     }

    //     // 4) Go to the user-selected target (B)
    //     _missionController.insertSimpleMissionItem(toRelative(targetCoord), idx++, true)

    //     // 5) DO_SERVO for the drop (keep your helper; if possible, make it command-only)
    //     _missionController.insertSimpleMissionItemServo(targetCoord, idx++, false)

    //     // 6) Land back at A (or change to a different LZ if you have one)
    //     _missionController.insertLandItem(A, idx++, false)

    //     backend.dropPointSelected = true

    //     console.log(_planMasterController.getJson())
    // }


    function insertSimpleItemAfterCurrent(coordinate) {
        // identical guard to your stock code
        if (!backend.dropPointSelected
            && _missionController.currentPlanViewVIIndex === 0
            && _missionController.currentPlanViewSeqNum === 0
            && !_missionController.containsItems) {

            var vehicleCoordinate = globals.activeVehicle.coordinate

            // Feed A/B and compute C/D (backend sets lat/lon; we'll set REL altitudes here)
            backend.A = vehicleCoordinate
            backend.B = coordinate
            void backend.angle

            // ---- profile (REL) ----
            var takeoffRel = 2.0
            var haulRel    = Number(_planViewSettings.currentProfileAlt.rawValue) || 0
            var L          = Number(_planViewSettings.currentProfileCableLength.rawValue) || 0
            var angleDeg   = Number(_planViewSettings.currentProfileAngle.rawValue) || 0
            var theta      = angleDeg * Math.PI / 180

            // D altitude (REL): 2 m + L·sin(theta), clamped to haul
            var dAltRel = Math.min(haulRel, takeoffRel + (L > 0 ? L * Math.sin(theta) : 0))

            // helpers
            function _setLastItemAltRel(relAlt) {
                var n = _missionController.visualItems.count
                if (!n) return
                var vi = _missionController.visualItems.get(n - 1)
                try {
                    if (vi.hasOwnProperty("altitudeMode")) {
                        vi.altitudeMode = QGroundControl.AltitudeModeRelative
                    }
                    if (vi.altitude && vi.altitude.hasOwnProperty("rawValue")) {
                        vi.altitude.rawValue = relAlt
                    }
                } catch (e) { console.log("setLastItemAltRel failed:", e) }
            }
            function coordsAlmostEqual(c1, c2) {
                if (!c1 || !c2) return false
                var mPerDeg = 111320
                var dLat = (c1.latitude - c2.latitude) * mPerDeg
                var meanLatRad = ((c1.latitude + c2.latitude) * 0.5) * Math.PI / 180
                var dLon = (c1.longitude - c2.longitude) * mPerDeg * Math.cos(meanLatRad)
                var horiz = Math.sqrt(dLat*dLat + dLon*dLon)
                var dAlt = Math.abs((c1.altitude||0) - (c2.altitude||0))
                return horiz < 0.5 && dAlt < 0.5
            }
            // uses your existing toRelative(c)

            var nextIndex = 1

            // (1) TAKEOFF @ A (2 m)
            _missionController.insertTakeoffItem(vehicleCoordinate, nextIndex++, true)
            _setLastItemAltRel(takeoffRel)

            // (2) Slow/takeoff speed for the climb leg. This replaces the old
            // waypoint above A so mission resume cannot fly backward to launch.
            _missionController.insertSimpleMissionItemSpeed(vehicleCoordinate, nextIndex++, true)

            // (3) D (cable-end) -> worker applies haul speed for the next leg
            _missionController.insertSimpleMissionItem(toRelative(backend.D), nextIndex++, true)
            _setLastItemAltRel(dAltRel)  // ensure correct REL altitude at cable end

            // (4) C (haul altitude), only if meaningfully different from D.
            if (!coordsAlmostEqual(backend.D, backend.C)) {
                _missionController.insertSimpleMissionItem(toRelative(backend.C), nextIndex++, true)
                _setLastItemAltRel(haulRel)
            }

            // ---- descend-to-drop logic ----
            var useDropAlt = Boolean(_planViewSettings.currentProfileUseDropAlt.rawValue)
            var dropAltRel = Number(_planViewSettings.currentProfileDropAlt.rawValue)
            if (!isFinite(dropAltRel) || dropAltRel <= 0) dropAltRel = 10.0
                if (useDropAlt) {
                var descentAngleDeg = 45.0    // TODO: tune later (shallower -> needs more distance)
                var descentAngleRad = descentAngleDeg * Math.PI / 180
                var margin = 5.0              // meters safety margin

                // prevHigh is the last "haul altitude" coordinate before approaching B
                // If C wasn't inserted, we approach from D.
                var prevHigh = coordsAlmostEqual(backend.D, backend.C) ? backend.D : backend.C

                // distance from prevHigh -> B
                var segmentDist = prevHigh.distanceTo(coordinate)

                // how much vertical drop we need (REL)
                var verticalDrop = Math.max(0, haulRel - dropAltRel)

                // how much horizontal distance is needed to do that drop at the chosen descent angle
                var needHoriz = 0
                if (verticalDrop > 0.01 && Math.abs(Math.tan(descentAngleRad)) > 1e-6) {
                    needHoriz = verticalDrop / Math.tan(descentAngleRad)
                }

                // (5) either:
                //   - insert P@haul then B@dropAlt, OR
                //   - if not enough distance, go straight to B@haul like before
                if (needHoriz > 1.0 && segmentDist >= (needHoriz + margin)) {
                    // compute point P which is "needHoriz" meters BEFORE B towards prevHigh
                    var azBack = coordinate.azimuthTo(prevHigh)
                    var pCoord = coordinate.atDistanceAndAzimuth(needHoriz, azBack)

                    // P at haul altitude (REL)
                    _missionController.insertSimpleMissionItem(toRelative(pCoord), nextIndex++, true)
                    _setLastItemAltRel(haulRel)

                    // B at drop altitude (REL 10m)
                    _missionController.insertSimpleMissionItem(toRelative(coordinate), nextIndex++, true)
                    _setLastItemAltRel(dropAltRel)
                }else{
                    _missionController.insertSimpleMissionItem(toRelative(coordinate), nextIndex++, true)
                    _setLastItemAltRel(haulRel)
                }
            }else {
                // fallback: old behavior (B at haul altitude)
                    _missionController.insertSimpleMissionItem(toRelative(coordinate), nextIndex++, true)
                    _setLastItemAltRel(haulRel)
            }

            // (6) DO_SET_SERVO (drop) at B
            _missionController.insertSimpleMissionItemServo(coordinate, nextIndex++, false)

            // (7) LAND back at A
            _missionController.insertLandItem(vehicleCoordinate, nextIndex++, false)
            // bookkeeping
            globals.pointToAdd.lat = coordinate.latitude
            globals.pointToAdd.lon = coordinate.longitude
            backend.dropPointSelected = true

        } else {
            _planMasterController.removeAllFromVehicle()
            _missionController.setCurrentPlanViewSeqNum(0, true)
            backend.dropPointSelected = false
            mainWindow.showComponentDialog(missionWasNotClean, "Clean", mainWindow.showDialogDefaultWidth, StandardButton.Yes)
        }

        console.log(_planMasterController.getJson())
    }


    // function rebuildDropPlan(newTargetCoord) {
    //     // Clear current plan
    //     _planMasterController.removeAllFromVehicle()
    //     _missionController.setCurrentPlanViewSeqNum(0, true)

    //     // Reset whatever flags you use
    //     if (backend.dropPointSelected) backend.dropPointSelected = false

    //     // Rebuild from scratch using your existing builder
    //     insertSimpleItemAfterCurrent(newTargetCoord)
    // }

    function toRelative(c) {
        const homeAlt = globals.activeVehicle.homePosition.altitude; // MSL
        return QtPositioning.coordinate(c.latitude, c.longitude, c.altitude - homeAlt);
    }

    function insertROIAfterCurrent(coordinate) {
        var nextIndex = _missionController.currentPlanViewVIIndex + 1
        _missionController.insertROIMissionItem(coordinate, nextIndex, true /* makeCurrentItem */)
    }

    function insertCancelROIAfterCurrent() {
        var nextIndex = _missionController.currentPlanViewVIIndex + 1
        _missionController.insertCancelROIMissionItem(nextIndex, true /* makeCurrentItem */)
    }

    function insertComplexItemAfterCurrent(complexItemName) {
        var nextIndex = _missionController.currentPlanViewVIIndex + 1
        _missionController.insertComplexMissionItem(complexItemName, mapCenter(), nextIndex, true /* makeCurrentItem */)
    }

    function insertTakeItemAfterCurrent() {
        var nextIndex = _missionController.currentPlanViewVIIndex + 1
        _missionController.insertTakeoffItem(mapCenter(), nextIndex, true /* makeCurrentItem */)
    }

    function insertLandItemAfterCurrent() {
        var nextIndex = _missionController.currentPlanViewVIIndex + 1
        _missionController.insertLandItem(mapCenter(), nextIndex, true /* makeCurrentItem */)
    }


    function selectNextNotReady() {
        var foundCurrent = false
        for (var i=0; i<_missionController.visualItems.count; i++) {
            var vmi = _missionController.visualItems.get(i)
            if (vmi.readyForSaveState === VisualMissionItem.NotReadyForSaveData) {
                _missionController.setCurrentPlanViewSeqNum(vmi.sequenceNumber, true)
                break
            }
        }
    }

    property int _moveDialogMissionItemIndex

    QGCFileDialog {
        id:             fileDialog
        folder:         _appSettings ? _appSettings.missionSavePath : ""

        property bool planFiles: true    ///< true: working with plan files, false: working with kml file

        onAcceptedForSave: {
            if (planFiles) {
                _planMasterController.saveToFile(file)
            } else {
                _planMasterController.saveToKml(file)
            }
            close()
        }

        onAcceptedForLoad: {
            _planMasterController.loadFromFile(file)
            _planMasterController.fitViewportToItems()
            _missionController.setCurrentPlanViewSeqNum(0, true)
            close()
        }
    }

    Component {
        id: moveDialog
        QGCViewDialog {
            function accept() {
                var toIndex = toCombo.currentIndex
                if (toIndex === 0) {
                    toIndex = 1
                }
                _missionController.moveMissionItem(_moveDialogMissionItemIndex, toIndex)
                hideDialog()
            }
            Column {
                anchors.left:   parent.left
                anchors.right:  parent.right
                spacing:        ScreenTools.defaultFontPixelHeight

                QGCLabel {
                    anchors.left:   parent.left
                    anchors.right:  parent.right
                    wrapMode:       Text.WordWrap
                    text:           qsTr("Move the selected mission item to the be after following mission item:")
                }

                QGCComboBox {
                    id:             toCombo
                    model:          _visualItems.count
                    currentIndex:   _moveDialogMissionItemIndex
                }
            }
        }
    }

    Item {
        id:             panel
        anchors.fill:   parent

        FlightMap {
            id:                         editorMap
            anchors.fill:               parent
            mapName:                    "MissionEditor"
            allowGCSLocationCenter:     true
            allowVehicleLocationCenter: true
            planView:                   true

            zoomLevel:                  QGroundControl.flightMapZoom
            center:                     QGroundControl.flightMapPosition

            // This is the center rectangle of the map which is not obscured by tools
            property rect centerViewport:   Qt.rect(_leftToolWidth + _margin,  _margin, editorMap.width - _leftToolWidth - _rightToolWidth - (_margin * 2), (terrainStatus.visible ? terrainStatus.y : height - _margin) - _margin)

            property real _leftToolWidth:       toolStrip.x + toolStrip.width
            property real _rightToolWidth:      rightPanel.width + rightPanel.anchors.rightMargin
            property real _nonInteractiveOpacity:  0.5

            // Initial map position duplicates Fly view position
            Component.onCompleted: editorMap.center = QGroundControl.flightMapPosition

            QGCMapPalette { id: mapPal; lightColors: editorMap.isSatelliteMap }

            onZoomLevelChanged: {
                QGroundControl.flightMapZoom = zoomLevel
                updateAirspace(false)
            }
            onCenterChanged: {
                QGroundControl.flightMapPosition = center
                updateAirspace(false)
            }

            MouseArea {
                anchors.fill: parent

                pressAndHoldInterval: 2000   // 2 seconds
                property bool _didLongPress: false

                onPressed: _didLongPress = false

                onPressAndHold: {
                    _didLongPress = true

                    editorMap.focus = true
                    var coordinate = editorMap.toCoordinate(Qt.point(mouse.x, mouse.y), false)

                    // Keep them numeric (toFixed returns a string)
                    coordinate.latitude  = Number(coordinate.latitude.toFixed(_decimalPlaces))
                    coordinate.longitude = Number(coordinate.longitude.toFixed(_decimalPlaces))
                    coordinate.altitude  = Number(coordinate.altitude.toFixed(_decimalPlaces))

                    // do your long-press action here
                    // e.g. openHistory() or show a context menu
                    //openHistory()
                    _newDropPointCoord = coordinate
                    mainWindow.showPopupDialogFromComponent(addDropPointPopUp)
                    //mainWindow.showPopupDialog(addDropPointPopUpDialog, editorMap)
                }

                onClicked: {
                    if (_didLongPress) return

                    editorMap.focus = true
                    var coordinate = editorMap.toCoordinate(Qt.point(mouse.x, mouse.y), false)

                    coordinate.latitude  = Number(coordinate.latitude.toFixed(_decimalPlaces))
                    coordinate.longitude = Number(coordinate.longitude.toFixed(_decimalPlaces))
                    coordinate.altitude  = Number(coordinate.altitude.toFixed(_decimalPlaces))

                    switch (_editingLayer) {
                    case _layerMission:
                        if (addWaypointRallyPointAction.checked) {
                            insertSimpleItemAfterCurrent(coordinate)
                        } else if (_addROIOnClick) {
                            insertROIAfterCurrent(coordinate)
                            _addROIOnClick = false
                        } else if (history.checked) {
                            openHistory()
                        }
                        break
                    case _layerRallyPoints:
                        if (_rallyPointController.supported && addWaypointRallyPointAction.checked) {
                            _rallyPointController.addPoint(coordinate)
                        }
                        break
                    }
                }
            }

            MapItemView {
                id: historyDropPoints
                model: backend.dropPoints
                property var dummyModel: backend.dropPoints
                visible: QGroundControl.settingsManager.flightMapSettings.enableHistory.value

                delegate: MapQuickItem {
                    coordinate: QtPositioning.coordinate(
                        historyDropPoints.dummyModel[index].lat,
                        historyDropPoints.dummyModel[index].lon
                    )

                    sourceItem: Item {
                        id: pinRoot

                        // 30% smaller than before
                        property real pinScale: 1.5 * 0.7
                        property real pinBase:  ScreenTools.defaultFontPixelHeight * 1.5

                        // scale the whole marker (pin + badge + text)
                        width:  Math.round(pinBase * pinScale)
                        height: Math.round(pinBase * pinScale)

                        // rating 0..5, always finite
                        property int ratingVal: Math.max(0, Math.min(5, Math.round(
                            Number(historyDropPoints.dummyModel[index] && historyDropPoints.dummyModel[index].rating) || 0
                        )))

                        // --- SVG pin icon ---
                        Image {
                            id: pinImg
                            anchors.fill: parent
                            source: "qrc:/qmlimages/GeoTagIcon"   // use exactly your qrc key
                            smooth: true
                            mipmap: true

                            // Helps SVG render at the correct size (less blur / fewer oddities)
                            sourceSize.width:  pinRoot.width
                            sourceSize.height: pinRoot.height
                        }

                        // --- Badge (circle + number) ---
                        Rectangle {
                            id: badge
                            width:  Math.round(pinRoot.width * 0.50)
                            height: width
                            radius: width / 2

                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.top
                            anchors.topMargin: Math.round(pinRoot.height * 0.18)

                            color: "white"
                            border.color: "black"
                            border.width: Math.max(1, Math.round(pinRoot.width * 0.03))

                            Text {
                                anchors.centerIn: parent
                                text: String(pinRoot.ratingVal)
                                color: "black"
                                font.pixelSize: Math.round(badge.height * 0.72)
                                font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        // keep your original offset so the tip points to the map coordinate
                        x: -width / 2
                        y: -height

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                console.log("ICON PRESSED")
                                historyItemData = historyDropPoints.dummyModel[index]
                                historyItemData.index = index
                                mainWindow.showPopupDialogFromComponent(historyItemPopUp)
                            }
                        }
                    }
                }

                onModelChanged: {
                    console.log("Model updated", model)
                    historyDropPoints.dummyModel = backend.dropPoints
                }
            }

            Component {
                id: historyItemPopUp

                QGCPopupDialog {
                    id:         historyItemPopUpDialog
                    title:      qsTr(historyItemData.label)
                    buttons:    StandardButton.Close

                    ColumnLayout {
                        //spacing: _margins

                        GridLayout {
                            id:     gridLayout
                            flow:   GridLayout.TopToBottom
                            rows:   5

                            QGCLabel {
                                text:               qsTr("Rating:")
                                visible:            true
                                //onVisibleChanged:   gridLayout.dynamicRows += visible ? 1 : -1
                            }


                            QGCLabel {
                                text:               "Times used:"
                                visible:            true
                                //onVisibleChanged:   gridLayout.dynamicRows += visible ? 1 : -1
                            }

                            QGCLabel {
                                text:               "Coordinates:"
                                visible: true
                            }


                            QGCLabel {
                                text: "Note:"
                            }

                            QGCButton {
                                text: "Use again"
                                enabled: globals.activeVehicle && globals.activeVehicle.coordinate.isValid
                                onClicked:{
                                    if(globals.activeVehicle && globals.activeVehicle.coordinate.isValid){
                                        historyItemPopUpDialog.hideDialog()
                                        insertSimpleItemAfterCurrent(QtPositioning.coordinate(historyItemData.lat, historyItemData.lon))
                                    }
                                }
                            }


                            RowLayout {
                                id: ratingRow
                                spacing: 8

                                RowLayout {
                                    id: starBar
                                    property int maxStars: 5
                                    // Don't rely on a non-notifyable binding; seed once:
                                    property int value: historyItemData.rating
                                    readonly property string starIcon: "/InstrumentValueIcons/star-full.svg"

                                    function setRating(v) {
                                        var nv = Math.max(0, Math.min(maxStars, v))
                                        if (value === nv && historyItemData.rating === nv) return

                                        // 1) update local reactive value so UI changes immediately
                                        value = nv

                                        // 2) keep your data + backend in sync
                                        historyItemData.rating = nv
                                        rating.text = nv
                                        backend.changeRating(historyItemData.index, nv)
                                    }

                                    // 3) if rating can change from outside (after save/load), resync UI:
                                    Connections {
                                        target: backend
                                        onDropPointsChanged: {
                                            // pull fresh value from your item, then reflect locally
                                            starBar.value = historyItemData.rating
                                        }
                                    }

                                    spacing: 6

                                    Repeater {
                                        model: starBar.maxStars
                                        delegate: Item {
                                            width: 80; height: 80
                                            property int starIndex: index + 1

                                            QGCColoredImage {
                                                anchors.fill: parent
                                                source: starBar.starIcon
                                                fillMode: Image.PreserveAspectFit
                                                // Drive transparency via color alpha (reliably updates)
                                                color: starBar.value >= starIndex ? Qt.rgba(0, 0, 0, 1) : Qt.rgba(0.7, 0.7, 0.7, 1)
                                            }
                                            MouseArea {
                                                anchors.fill: parent
                                                onClicked: starBar.setRating(starIndex)
                                                hoverEnabled: true
                                            }
                                        }
                                    }
                                }

                                QGCLabel {
                                    id: rating
                                    text: historyItemData.rating
                                    visible: true
                                }
                            }

                            QGCLabel {
                                text:               historyItemData.counter
                                visible:            true
                            }

                            QGCLabel {
                                text: Number(historyItemData.lat).toFixed(7) + " " + Number(historyItemData.lon).toFixed(7)
                            }

                            TextField {
                                id: noteField
                                Layout.fillWidth: true
                                Layout.preferredWidth: ScreenTools.defaultFontPixelHeight*20
                                font.pixelSize: ScreenTools.defaultFontPixelHeight * 0.8
                                text: historyItemData.note ? historyItemData.note : ""

                                onTextChanged: {
                                    if (text.length > 50) {
                                        var pos = cursorPosition
                                        text = text.substring(0, 50)
                                        cursorPosition = Math.min(pos, text.length)
                                    }
                                }

                                onAccepted: {
                                    backend.changeNote(historyItemData.index, text)
                                    historyItemPopUpDialog.hideDialog()
                                }
                            }

                            QGCButton {
                                text: "Remove Drop point"
                                onClicked:{
                                    backend.removeDropPoint(historyItemData.index)
                                    historyItemPopUpDialog.hideDialog()
                                }
                            }
                            // QGCButton {
                            //     text: "Change rating"
                            // }

                        }
                    }
                }
            }

            Component {
                id: addDropPointPopUp

                QGCPopupDialog {
                    id:      addDropPointPopUpDialog
                    title:   qsTr("Add drop point")
                    buttons: StandardButton.Close

                    ColumnLayout {
                        GridLayout {
                            flow: GridLayout.TopToBottom
                            rows: 4
                            columnSpacing: ScreenTools.defaultFontPixelWidth
                            rowSpacing: ScreenTools.defaultFontPixelHeight * 0.5

                            QGCLabel { text: qsTr("Latitude:") }
                            QGCTextField {
                                id: latField
                                text: (_newDropPointCoord && _newDropPointCoord.isValid)
                                          ? Number(_newDropPointCoord.latitude).toFixed(8)
                                          : ""
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                onEditingFinished: {
                                    var v = Number(text)
                                    if (isFinite(v) && v >= -90 && v <= 90) {
                                        _newDropPointCoord = QtPositioning.coordinate(v, _newDropPointCoord.longitude)
                                    } else {
                                        // revert
                                        text = Number(_newDropPointCoord.latitude).toFixed(8)
                                    }
                                }
                            }

                            QGCLabel { text: qsTr("Longitude:") }
                            QGCTextField {
                                id: lonField
                                text: (_newDropPointCoord && _newDropPointCoord.isValid)
                                          ? Number(_newDropPointCoord.longitude).toFixed(8)
                                          : ""
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                onEditingFinished: {
                                    var v = Number(text)
                                    if (isFinite(v) && v >= -180 && v <= 180) {
                                        _newDropPointCoord = QtPositioning.coordinate(_newDropPointCoord.latitude, v)
                                    } else {
                                        text = Number(_newDropPointCoord.longitude).toFixed(8)
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.topMargin: 12
                            spacing: 10

                            QGCButton {
                                text: qsTr("Cancel")
                                onClicked: addDropPointPopUpDialog.hideDialog()
                            }

                            QGCButton {
                                text: qsTr("Add")
                                enabled: _newDropPointCoord && isFinite(_newDropPointCoord.latitude) && isFinite(_newDropPointCoord.longitude)
                                onClicked: {
                                    backend.addDropPoint(
                                        "Point",
                                        Number(_newDropPointCoord.latitude),
                                        Number(_newDropPointCoord.longitude)
                                    )
                                    addDropPointPopUpDialog.hideDialog()
                                }
                            }
                        }
                    }
                }
            }

            function _seqOfDropTargetB() {
                // We want the mission item right before the DO_SET_SERVO item.
                // Find first servo command, then return previous item's sequenceNumber.
                var n = _missionController.visualItems.count
                if (!n) return -1

                for (var i = 0; i < n; i++) {
                    var vi = _missionController.visualItems.get(i)
                    if (!vi) continue

                    // Most QGC VisualMissionItems expose command for SimpleMissionItem
                    // Depending on your fork, it may be vi.command or vi.missionItem.command
                    var cmd = -1
                    try {
                        if (vi.command !== undefined) cmd = vi.command
                        else if (vi.missionItem && vi.missionItem.command !== undefined) cmd = vi.missionItem.command
                    } catch (e) {}

                    if (cmd === MAV_CMD_DO_SET_SERVO) {
                        // previous visual item is the B waypoint
                        if (i > 0) {
                            var prev = _missionController.visualItems.get(i - 1)
                            return prev ? prev.sequenceNumber : -1
                        }
                        return -1
                    }
                }
                return -1
            }


            function _isSelectableAndDraggable(seq) {
                var bSeq = _seqOfDropTargetB()
                return seq === bSeq
            }

            // Add the mission item visuals to the map
            Repeater {
                model: _missionController.visualItems
                delegate: MissionItemMapVisual {
                    map:         editorMap
                    onClicked: {
                        if (_isSelectableAndDraggable(sequenceNumber)) {
                            _missionController.setCurrentPlanViewSeqNum(sequenceNumber, false)
                        }
                    }
                    opacity:     _editingLayer == _layerMission ? 1 : editorMap._nonInteractiveOpacity
                    interactive: _editingLayer == _layerMission
                    vehicle:     _planMasterController.controllerVehicle
                }
            }

            // Add lines between waypoints
            MissionLineView {
                showSpecialVisual:  _missionController.isROIBeginCurrentItem
                model:              _missionController.simpleFlightPathSegments
                opacity:            _editingLayer == _layerMission ? 1 : editorMap._nonInteractiveOpacity
            }

            // Direction arrows in waypoint lines
            MapItemView {
                model: _editingLayer == _layerMission ? _missionController.directionArrows : undefined

                delegate: MapLineArrow {
                    fromCoord:      object ? object.coordinate1 : undefined
                    toCoord:        object ? object.coordinate2 : undefined
                    arrowPosition:  3
                    z:              QGroundControl.zOrderWaypointLines + 1
                }
            }

            // Incomplete segment lines
            MapItemView {
                model: _missionController.incompleteComplexItemLines

                delegate: MapPolyline {
                    path:       [ object.coordinate1, object.coordinate2 ]
                    line.width: 1
                    line.color: "red"
                    z:          QGroundControl.zOrderWaypointLines
                    opacity:    _editingLayer == _layerMission ? 1 : editorMap._nonInteractiveOpacity
                }
            }

            // UI for splitting the current segment
            MapQuickItem {
                id:             splitSegmentItem
                anchorPoint.x:  sourceItem.width / 2
                anchorPoint.y:  sourceItem.height / 2
                z:              QGroundControl.zOrderWaypointLines + 1
                visible:         false//_editingLayer == _layerMission

                sourceItem: SplitIndicator {
                    onClicked:  _missionController.insertSimpleMissionItem(splitSegmentItem.coordinate,
                                                                           _missionController.currentPlanViewVIIndex,
                                                                           true /* makeCurrentItem */)
                }

                function _updateSplitCoord() {
                    if (_missionController.splitSegment) {
                        var distance = _missionController.splitSegment.coordinate1.distanceTo(_missionController.splitSegment.coordinate2)
                        var azimuth = _missionController.splitSegment.coordinate1.azimuthTo(_missionController.splitSegment.coordinate2)
                        splitSegmentItem.coordinate = _missionController.splitSegment.coordinate1.atDistanceAndAzimuth(distance / 2, azimuth)
                    } else {
                        coordinate = QtPositioning.coordinate()
                    }
                }

                Connections {
                    target:                 _missionController
                    function onSplitSegmentChanged()  { splitSegmentItem._updateSplitCoord() }
                }

                Connections {
                    target:                 _missionController.splitSegment
                    function onCoordinate1Changed()   { splitSegmentItem._updateSplitCoord() }
                    function onCoordinate2Changed()   { splitSegmentItem._updateSplitCoord() }
                }
            }

            // Add the vehicles to the map
            MapItemView {
                model: QGroundControl.multiVehicleManager.vehicles
                delegate: VehicleMapItem {
                    vehicle:        object
                    coordinate:     object.coordinate
                    map:            editorMap
                    size:           ScreenTools.defaultFontPixelHeight * 3
                    z:              QGroundControl.zOrderMapItems - 1
                }
            }



            GeoFenceMapVisuals {
                map:                    editorMap
                myGeoFenceController:   _geoFenceController
                interactive:            _editingLayer == _layerGeoFence
                homePosition:           _missionController.plannedHomePosition
                planView:               true
                opacity:                _editingLayer != _layerGeoFence ? editorMap._nonInteractiveOpacity : 1
            }

            RallyPointMapVisuals {
                map:                    editorMap
                myRallyPointController: _rallyPointController
                interactive:            _editingLayer == _layerRallyPoints
                planView:               true
                opacity:                _editingLayer != _layerRallyPoints ? editorMap._nonInteractiveOpacity : 1
            }

            // Airspace overlap support
            MapItemView {
                model:              _airspaceEnabled && QGroundControl.airspaceManager.airspaceVisible ? QGroundControl.airspaceManager.airspaces.circles : []
                delegate: MapCircle {
                    center:         object.center
                    radius:         object.radius
                    color:          object.color
                    border.color:   object.lineColor
                    border.width:   object.lineWidth
                }
            }

            MapItemView {
                model:              _airspaceEnabled && QGroundControl.airspaceManager.airspaceVisible ? QGroundControl.airspaceManager.airspaces.polygons : []
                delegate: MapPolygon {
                    path:           object.polygon
                    color:          object.color
                    border.color:   object.lineColor
                    border.width:   object.lineWidth
                }
            }
        }

        //-----------------------------------------------------------
        // Left tool strip
        ToolStrip {
            id:                 toolStrip
            anchors.left:       parent.left
            anchors.top:        parent.top
            z:                  QGroundControl.zOrderWidgets
            maxHeight:          (parent.height - y) / scaleFactor
            title:              qsTr("Plan")


            // --- added for scaling ---
            property real scaleFactor: 1.5
            transformOrigin: Item.TopLeft
            scale: scaleFactor
            // --- end added ---


            readonly property int flyButtonIndex:       0
            readonly property int fileButtonIndex:      1
            readonly property int takeoffButtonIndex:   2
            readonly property int waypointButtonIndex:  3
            readonly property int roiButtonIndex:       4
            readonly property int patternButtonIndex:   5
            readonly property int landButtonIndex:      6
            readonly property int centerButtonIndex:    7

            property bool _isRallyLayer:    _editingLayer == _layerRallyPoints
            property bool _isMissionLayer:  _editingLayer == _layerMission

            ToolStripActionList {
                id: toolStripActionList
                model: [
                    ToolStripAction {
                        text:           qsTr("Fly")
                        iconSource:     "/qmlimages/PaperPlane.svg"
                        onTriggered:    mainWindow.showFlyView()
                    },
                    ToolStripAction {
                        text:                   qsTr("Profiles")
                        enabled:                !_planMasterController.syncInProgress
                        visible:                true
                        showAlternateIcon:      _planMasterController.dirty
                        iconSource:             "/qmlimages/MapSync.svg"
                        alternateIconSource:    "/qmlimages/MapSyncChanged.svg"
                        dropPanelComponent:     syncDropPanel
                    },
                    /*ToolStripAction {
                        text:       qsTr("Takeoff")
                        iconSource: "/res/takeoff.svg"
                        enabled:    _missionController.isInsertTakeoffValid
                        visible:    toolStrip._isMissionLayer && !_planMasterController.controllerVehicle.rover
                        onTriggered: {
                            toolStrip.allAddClickBoolsOff()
                            insertTakeItemAfterCurrent()
                        }
                    },*/
                    ToolStripAction {
                        id:                 addWaypointRallyPointAction
                        text:               _editingLayer == _layerRallyPoints ? qsTr("Rally Point") : qsTr("Cast")
                        iconSource:         "/qmlimages/MapAddMission.svg"
                        enabled:            globals.activeVehicle && globals.activeVehicle.coordinate.isValid
                        visible:            true//toolStrip._isRallyLayer || toolStrip._isMissionLayer
                        checkable:          true
                        //dropPanelComponent:     syncDropPanel

                    },
                    /*ToolStripAction {
                        text:               _missionController.isROIActive ? qsTr("Cancel ROI") : qsTr("ROI")
                        iconSource:         "/qmlimages/MapAddMission.svg"
                        enabled:            !_missionController.onlyInsertTakeoffValid
                        visible:            toolStrip._isMissionLayer && _planMasterController.controllerVehicle.roiModeSupported
                        checkable:          !_missionController.isROIActive
                        onCheckedChanged:   _addROIOnClick = checked
                        onTriggered: {
                            if (_missionController.isROIActive) {
                                toolStrip.allAddClickBoolsOff()
                                insertCancelROIAfterCurrent()
                            }
                        }
                        property bool myAddROIOnClick: _addROIOnClick
                        onMyAddROIOnClickChanged: checked = _addROIOnClick
                    },*/
                    /*ToolStripAction {
                        text:               _singleComplexItem ? _missionController.complexMissionItemNames[0] : qsTr("Pattern")
                        iconSource:         "/qmlimages/MapDrawShape.svg"
                        enabled:            _missionController.flyThroughCommandsAllowed
                        visible:            toolStrip._isMissionLayer
                        dropPanelComponent: _singleComplexItem ? undefined : patternDropPanel
                        onTriggered: {
                            toolStrip.allAddClickBoolsOff()
                            if (_singleComplexItem) {
                                insertComplexItemAfterCurrent(_missionController.complexMissionItemNames[0])
                            }
                        }
                    },*/
                    /*ToolStripAction {
                        text:       _planMasterController.controllerVehicle.multiRotor ? qsTr("Return") : qsTr("Land")
                        iconSource: "/res/rtl.svg"
                        enabled:    _missionController.isInsertLandValid
                        visible:    toolStrip._isMissionLayer
                        onTriggered: {
                            toolStrip.allAddClickBoolsOff()
                            insertLandItemAfterCurrent()
                        }
                    },*/
                    ToolStripAction {
                        text:               qsTr("Center")
                        iconSource:         "/qmlimages/MapCenter.svg"
                        enabled:            true
                        visible:            true
                        dropPanelComponent: centerMapDropPanel
                    },

                    // ToolStripAction {
                    //     id:                 history
                    //     text:               qsTr("History")
                    //     iconSource:         "/qmlimages/MapAddMission.svg"
                    //     enabled:            true
                    //     visible:            true
                    //     checkable:          true
                    //     onCheckedChanged: {
                    //         if(checked){
                    //             // _planMasterController.removeAllFromVehicle()
                    //             // _missionController.setCurrentPlanViewSeqNum(0, true)
                    //             // backend.dropPointSelected = false
                    //             QGroundControl.settingsManager.flightMapSettings.enableHistory.value = true
                    //         } else {
                    //             QGroundControl.settingsManager.flightMapSettings.enableHistory.value = false
                    //         }
                    //     }

                    // },

                    ToolStripAction {
                        id:                 clearToolStripAction
                        text:               qsTr("Clear")
                        iconSource:         "/qmlimages/DatalinkLossLight.svg"
                        enabled:            true
                        visible:            true
                        onTriggered:{
                            mainWindow.showComponentDialog(clearVehicleMissionDialog, text, mainWindow.showDialogDefaultWidth, StandardButton.Yes | StandardButton.Cancel)
                        }
                    }

                ]
            }

            model: toolStripActionList.model

            function allAddClickBoolsOff() {
                _addROIOnClick =        false
                addWaypointRallyPointAction.checked = false
                history.checked = false
            }

            onDropped: allAddClickBoolsOff()
        }

        //-----------------------------------------------------------
        // Right pane for mission editing controls
        Rectangle {
            id:                 rightPanel
            height:             parent.height
            width:              _rightPanelWidth
            color:              qgcPal.window
            opacity:            layerTabBar.visible ? 0.2 : 0
            anchors.bottom:     parent.bottom
            anchors.right:      parent.right
            anchors.rightMargin: _toolsMargin
            visible: enableDebug//true
        }
        //-------------------------------------------------------
        // Right Panel Controls
        Item {
            anchors.fill:           rightPanel
            anchors.topMargin:      _toolsMargin
            /*DeadMouseArea {
                anchors.fill:   parent
            }*/
            Column {
                id:                 rightControls
                spacing:            ScreenTools.defaultFontPixelHeight * 0.5
                anchors.left:       parent.left
                anchors.right:      parent.right
                anchors.top:        parent.top
                //-------------------------------------------------------
                // Airmap Airspace Control
                AirspaceControl {
                    id:             airspaceControl
                    width:          parent.width
                    visible:        _airspaceEnabled
                    planView:       true
                    showColapse:    true
                }
                //-------------------------------------------------------
                // Mission Controls (Colapsed)
                Rectangle {
                    width:      parent.width
                    height:     planControlColapsed ? colapsedRow.height + ScreenTools.defaultFontPixelHeight : 0
                    color:      qgcPal.missionItemEditor
                    radius:     _radius
                    visible:    enableDebug ? planControlColapsed && _airspaceEnabled : false
                    Row {
                        id:                     colapsedRow
                        spacing:                ScreenTools.defaultFontPixelWidth
                        anchors.left:           parent.left
                        anchors.leftMargin:     ScreenTools.defaultFontPixelWidth
                        anchors.verticalCenter: parent.verticalCenter
                        QGCColoredImage {
                            width:              height
                            height:             ScreenTools.defaultFontPixelWidth * 2.5
                            sourceSize.height:  height
                            source:             "qrc:/res/waypoint.svg"
                            color:              qgcPal.text
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        QGCLabel {
                            text:               qsTr("Plan")
                            color:              qgcPal.text
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    QGCColoredImage {
                        width:                  height
                        height:                 ScreenTools.defaultFontPixelWidth * 2.5
                        sourceSize.height:      height
                        source:                 QGroundControl.airmapSupported ? "qrc:/airmap/expand.svg" : ""
                        color:                  "white"
                        visible:                QGroundControl.airmapSupported
                        anchors.right:          parent.right
                        anchors.rightMargin:    ScreenTools.defaultFontPixelWidth
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    MouseArea {
                        anchors.fill:   parent
                        enabled:        QGroundControl.airmapSupported
                        onClicked: {
                            QGroundControl.airspaceManager.airspaceVisible = false
                        }
                    }
                }
                //-------------------------------------------------------
                // Mission Controls (Expanded)
                QGCTabBar {
                    id:         layerTabBar
                    width:      parent.width
                    visible:    enableDebug ? ((!planControlColapsed || !_airspaceEnabled) && QGroundControl.corePlugin.options.enablePlanViewSelector) : false
                    Component.onCompleted: currentIndex = 0
                    QGCTabButton {
                        text:       qsTr("Mission")
                    }
                    QGCTabButton {
                        text:       qsTr("Fence")
                        enabled:    _geoFenceController.supported
                    }
                    QGCTabButton {
                        text:       qsTr("Rally")
                        enabled:    _rallyPointController.supported
                    }
                }
            }
            //-------------------------------------------------------
            // Mission Item Editor
            Item {
                id:                     missionItemEditor
                anchors.left:           parent.left
                width: 1
                anchors.right:          parent.right
                anchors.top:            rightControls.bottom
                anchors.topMargin:      ScreenTools.defaultFontPixelHeight * 0.25
                anchors.bottom:         parent.bottom
                anchors.bottomMargin:   ScreenTools.defaultFontPixelHeight * 0.25
                visible:                _editingLayer == _layerMission && !planControlColapsed
                QGCListView {
                    id:                 missionItemEditorListView
                    anchors.fill:       parent
                    spacing:            ScreenTools.defaultFontPixelHeight / 4
                    orientation:        ListView.Vertical
                    model:              _missionController.visualItems
                    cacheBuffer:        Math.max(height * 2, 0)
                    clip:               true
                    currentIndex:       _missionController.currentPlanViewSeqNum
                    highlightMoveDuration: 250
                    visible:            enableDebug ? _editingLayer == _layerMission && !planControlColapsed : false
                    //-- List Elements
                    delegate: MissionItemEditor {
                        map:            editorMap
                        masterController:  _planMasterController
                        missionItem:    object
                        width:          enableDebug ? parent.width : 1
                        readOnly:       false
                        onClicked:      _missionController.setCurrentPlanViewSeqNum(object.sequenceNumber, false)
                        onRemove: {
                            var removeVIIndex = index
                            _missionController.removeVisualItem(removeVIIndex)
                            if (removeVIIndex >= _missionController.visualItems.count) {
                                removeVIIndex--
                            }
                        }
                        onSelectNextNotReadyItem:   selectNextNotReady()
                    }
                }
            }
            // GeoFence Editor
            GeoFenceEditor {
                anchors.top:            rightControls.bottom
                anchors.topMargin:      ScreenTools.defaultFontPixelHeight * 0.25
                anchors.bottom:         parent.bottom
                anchors.left:           parent.left
                anchors.right:          parent.right
                myGeoFenceController:   _geoFenceController
                flightMap:              editorMap
                visible:               _editingLayer == _layerGeoFence
            }

            // Rally Point Editor
            RallyPointEditorHeader {
                id:                     rallyPointHeader
                anchors.top:            rightControls.bottom
                anchors.topMargin:      ScreenTools.defaultFontPixelHeight * 0.25
                anchors.left:           parent.left
                anchors.right:          parent.right
                visible:                _editingLayer == _layerRallyPoints
                controller:             _rallyPointController
            }
            RallyPointItemEditor {
                id:                     rallyPointEditor
                anchors.top:            rallyPointHeader.bottom
                anchors.topMargin:      ScreenTools.defaultFontPixelHeight * 0.25
                anchors.left:           parent.left
                anchors.right:          parent.right
                visible:                _editingLayer == _layerRallyPoints && _rallyPointController.points.count
                rallyPoint:             _rallyPointController.currentRallyPoint
                controller:             _rallyPointController
            }
        }

        TerrainStatus {
            id:                 terrainStatus
            anchors.margins:    _toolsMargin
            anchors.leftMargin: 0
            anchors.left:       mapScale.left
            //anchors.right:      parrent.right
            //anchors.bottom:     parent.bottom
            height:             ScreenTools.defaultFontPixelHeight * 7
            missionController:  _missionController
            visible:           false// _internalVisible && _editingLayer === _layerMission && QGroundControl.corePlugin.options.showMissionStatus

            onSetCurrentSeqNum: _missionController.setCurrentPlanViewSeqNum(seqNum, true)

            property bool _internalVisible: _planViewSettings.showMissionItemStatus.rawValue

            function toggleVisible() {
                _internalVisible = !_internalVisible
                _planViewSettings.showMissionItemStatus.rawValue = _internalVisible
            }
        }

        MapScale {
            id:                     mapScale
            anchors.margins:        _toolsMargin
            //anchors.bottom:         terrainStatus.visible ? terrainStatus.top : parent.bottom
            anchors.top:            parent.top
            anchors.right:          parent.right// toolStrip.y + toolStrip.height + _toolsMargin > mapScale.y ? toolStrip.right: parent.left
            mapControl:             editorMap
            buttonsOnLeft:          false
            terrainButtonVisible:   _editingLayer === _layerMission
            terrainButtonChecked:   terrainStatus.visible
            onTerrainButtonClicked: terrainStatus.toggleVisible()
        }
    }


    Component {
        id: syncLoadFromFileOverwrite
        QGCViewMessage {
            id:         syncLoadFromVehicleCheck
            message:   qsTr("You have unsaved/unsent changes. Loading from a file will lose these changes. Are you sure you want to load from a file?")
            function accept() {
                hideDialog()
                _planMasterController.loadFromSelectedFile()
            }
        }
    }

    property var createPlanRemoveAllPromptDialogMapCenter
    property var createPlanRemoveAllPromptDialogPlanCreator
    Component {
        id: createPlanRemoveAllPromptDialog
        QGCViewMessage {
            message: qsTr("Are you sure you want to remove current plan and create a new plan? ")
            function accept() {
                createPlanRemoveAllPromptDialogPlanCreator.createPlan(createPlanRemoveAllPromptDialogMapCenter)
                hideDialog()
            }
        }
    }

    Component {
        id: clearVehicleMissionDialog
        QGCViewMessage {
            message: qsTr("Are you sure you want to clear the prepared mission from this app?")
            function accept() {
                _planMasterController.removeAllFromVehicle()
                _missionController.setCurrentPlanViewSeqNum(0, true)
                backend.dropPointSelected = false
                hideDialog()
            }
        }
    }

    Component {
        id: missionWasNotClean
        QGCViewMessage {
            message: qsTr("There is a chance that mission was not clean, we cleared it for you. Please try again to add drop point")
            function accept() {
                //_planMasterController.removeAllFromVehicle()
                //_missionController.setCurrentPlanViewSeqNum(0, true)
                hideDialog()
            }
        }
    }


    //- ToolStrip DropPanel Components

    Component {
        id: centerMapDropPanel

        CenterMapDropPanel {
            map:            editorMap
            fitFunctions:   mapFitFunctions
            transformOrigin: Item.TopLeft
            scale: 1.0 / toolStrip.scaleFactor
        }
    }

    Component {
        id: patternDropPanel

        ColumnLayout {
            spacing:    ScreenTools.defaultFontPixelWidth * 0.5

            QGCLabel { text: qsTr("Create complex pattern:") }

            Repeater {
                model: _missionController.complexMissionItemNames

                QGCButton {
                    text:               modelData
                    Layout.fillWidth:   true

                    onClicked: {
                        insertComplexItemAfterCurrent(modelData)
                        dropPanel.hide()
                    }
                }
            }
        } // Column
    }

    Component {
        id: syncDropPanel

        Item {
            id: panelRoot

            // Reasonable width so the background matches buttons
            width:  ScreenTools.defaultFontPixelWidth * 26

            readonly property real _vMargin:   ScreenTools.defaultFontPixelHeight * 2
            readonly property real _maxHeight: mainWindow
                                               ? (mainWindow.height - _vMargin) * 0.6
                                               : (ScreenTools.defaultFontPixelHeight * 35) * 0.6

            // Limit panel height to screen and let inner content scroll
            height: Math.min(columnHolder.implicitHeight + ScreenTools.defaultFontPixelHeight,
                             _maxHeight) * 1.5

            // Counter the 1.5x ToolStrip scale so this panel is "normal size"
            transformOrigin: Item.TopLeft
            scale: 1.0 / toolStrip.scaleFactor

            // 🔧 Key fix: implicit size must match *visible* (scaled) size
            implicitWidth:  width * scale
            implicitHeight: height * scale

            QGCFlickable {
                id: flick
                anchors.fill: parent
                contentWidth:  panelRoot.width/1.5
                contentHeight: columnHolder.implicitHeight
                clip:          true

                ColumnLayout {
                    id:         columnHolder
                    width:      flick.width - ScreenTools.defaultFontPixelWidth * 2
                    anchors.margins: ScreenTools.defaultFontPixelWidth
                    spacing:    _margin

                    property string _overwriteText: (_editingLayer == _layerMission) ?
                                                     qsTr("Mission overwrite") :
                                                     ((_editingLayer == _layerGeoFence) ?
                                                         qsTr("GeoFence overwrite") :
                                                         qsTr("Rally Points overwrite"))

                    QGCLabel {
                        id:                 unsavedChangedLabel
                        wrapMode:           Text.WordWrap
                        Layout.preferredWidth: _valueFieldWidth*2
                        text:               qsTr("You have unsaved changes.")
                        visible:            _planMasterController.dirty
                    }

                    RowLayout {
                        id:                 storageSection
                        Layout.fillWidth:   true
                        spacing:            ScreenTools.defaultFontPixelWidth * 0.5

                        QGCLabel {
                            text:                   qsTr("Profile")
                            Layout.fillWidth:       true
                        }

                        QGCButton {
                            text:                   qsTr("Edit")
                            Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 5
                            visible:                !_isNew && !_isEdit && !_isDel
                            onClicked: {
                                _isNew = false
                                _isDel = false
                                _isEdit = true
                            }
                        }

                        QGCButton {
                            text:                   qsTr("New")
                            Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 5
                            visible:                !_isNew && !_isEdit && !_isDel
                            onClicked: {
                                _isEdit = false
                                _isDel = false
                                _isNew = true
                            }
                        }
                    }

                    QGCTextField {
                        id: editTextField
                        text: qsTr(_planViewSettings.currentProfileName.rawValue)
                        Layout.fillWidth: true
                        visible: _isEdit
                    }

                    QGCComboBox {
                        id: scale
                        Layout.fillWidth: true
                        model: backend.profileList
                        visible: !_isNew && !_isEdit
                        Component.onCompleted: {
                            currentIndex = find(_planViewSettings.currentProfileName.rawValue)
                        }
                        onActivated: {
                            console.log(scale.currentText)
                            backend.currentProfile = scale.currentText
                        }
                    }

                    FactTextField {
                        id: newTextField
                        Layout.fillWidth: true
                        fact: _planViewSettings.newProfileName
                        visible: _isNew
                    }

                    GridLayout {
                        columns:            2
                        rowSpacing:         _margin
                        columnSpacing:      ScreenTools.defaultFontPixelWidth
                        visible:            storageSection.visible

                        QGCLabel { text: qsTr("Takeoff Angle:") }
                        FactTextField {
                            id: angleFactTextField
                            fact: _planViewSettings.currentProfileAngle
                            Layout.preferredWidth:  _valueFieldWidth
                            visible: !_isNew
                            enabled: _isEdit
                        }

                        FactTextField {
                            id: newAngleFactTextField
                            fact: _planViewSettings.newProfileAngle
                            Layout.preferredWidth:  _valueFieldWidth
                            visible: _isNew
                        }

                        QGCLabel { text: qsTr("Takeoff speed:") }
                        FactTextField {
                            id: winchFactTextField
                            fact: _planViewSettings.currentProfileTakeOffSpeed
                            Layout.preferredWidth:  _valueFieldWidth
                            visible: !_isNew
                            enabled: _isEdit
                        }
                        FactTextField {
                            id: newWinchFactTextField
                            fact: _planViewSettings.newProfileTakeOffSpeed
                            Layout.preferredWidth:  _valueFieldWidth
                            visible: _isNew
                        }

                        QGCLabel { text: qsTr("Backbone:") }
                        FactTextField {
                            id: cableLengthFactTextField
                            fact: _planViewSettings.currentProfileCableLength
                            Layout.preferredWidth:  _valueFieldWidth
                            visible: !_isNew
                            enabled: _isEdit
                        }
                        FactTextField {
                            id: newCableLengthFactTextField
                            fact: _planViewSettings.newProfileCableLength
                            Layout.preferredWidth:  _valueFieldWidth
                            visible: _isNew
                        }

                        QGCLabel { text: qsTr("Winch:") }
                        FactTextField {
                            fact: _isNew ? _planViewSettings.newProfileWinchLength
                                         : _planViewSettings.currentProfileWinchLength
                            Layout.preferredWidth: _valueFieldWidth
                            enabled: _isNew || _isEdit
                        }

                        QGCLabel { text: qsTr("Haul Altitude:") }
                        FactTextField {
                            id: altitudeFactTextField
                            fact: _planViewSettings.currentProfileAlt
                            Layout.preferredWidth:  _valueFieldWidth
                            visible: !_isNew
                            enabled: _isEdit
                        }
                        FactTextField {
                            id: newAltitudeFactTextField
                            fact: _planViewSettings.newProfileAlt
                            Layout.preferredWidth:  _valueFieldWidth
                            visible: _isNew
                        }

                        QGCLabel { text: qsTr("Haul Speed:") }
                        FactTextField {
                            id: speedFactTextField
                            fact: _planViewSettings.currentProfileSpeed
                            Layout.preferredWidth:  _valueFieldWidth
                            visible: !_isNew
                            enabled: _isEdit
                        }
                        FactTextField {
                            id: newSpeedFactTextField
                            fact: _planViewSettings.newProfileSpeed
                            Layout.preferredWidth:  _valueFieldWidth
                            visible: _isNew
                        }

                        QGCLabel { text: qsTr("Drop Descent:") }

                        FactCheckBox {
                            id: useDropAltCheck
                            fact: _isNew ? _planViewSettings.newProfileUseDropAlt
                                         : _planViewSettings.currentProfileUseDropAlt
                            enabled: _isNew || _isEdit
                        }

                        QGCLabel { text: qsTr("Drop Altitude:")}

                        FactTextField {
                            fact: _isNew ? _planViewSettings.newProfileDropAlt
                                         : _planViewSettings.currentProfileDropAlt
                            Layout.preferredWidth: _valueFieldWidth
                            enabled: (_isNew || _isEdit) && useDropAltCheck.checked
                            opacity: useDropAltCheck.checked ? 1.0 : 0.4
                        }

                    }

                    // --- Profile management buttons ---
                    GridLayout {
                        columns:            1
                        columnSpacing:      _margin
                        rowSpacing:         _margin
                        Layout.fillWidth:   true
                        visible:            true

                        QGCButton {
                            text: qsTr("SAVE values")
                            visible: _isEdit
                            Layout.fillWidth:   true
                            onClicked: {
                                backend.editProfile = editTextField.text
                                var copy = backend.profileList
                                scale.model = copy
                                scale.currentIndex = scale.find(_planViewSettings.currentProfileName.rawValue)
                                _isEdit = false
                            }
                        }

                        QGCButton {
                            text: qsTr("ADD profile")
                            visible: _isNew
                            Layout.fillWidth:   true
                            onClicked: {
                                backend.newProfile = newTextField.text
                                var copy = backend.profileList
                                scale.model = copy
                                scale.currentIndex = scale.find(_planViewSettings.currentProfileName.rawValue)
                                _isNew = false
                            }
                        }

                        QGCButton {
                            text:               qsTr("Delete profile")
                            Layout.fillWidth:   true
                            visible:            !_isNew && !_isEdit && !_isDel
                            onClicked:          _isDel = true
                        }

                        QGCLabel {
                            text: qsTr("Do you want to remove current profile?")
                            Layout.preferredWidth: _valueFieldWidth*2
                            visible: _isDel
                            wrapMode: Label.WordWrap
                        }

                        QGCButton {
                            text: qsTr("YES")
                            visible: _isDel
                            onClicked: {
                                backend.deleteProfile = _planViewSettings.currentProfileName.rawValue
                                var copy = backend.profileList
                                scale.model = copy
                                scale.currentIndex = scale.find(_planViewSettings.currentProfileName.rawValue)
                                _isDel = false
                            }
                        }

                        QGCButton {
                            text: qsTr("NO")
                            visible: _isDel
                            onClicked: _isDel = false
                        }
                    }

                    // spacer grid kept for layout compatibility
                    GridLayout {
                        columns:            2
                        rowSpacing:         _margin
                        columnSpacing:      ScreenTools.defaultFontPixelWidth
                        visible:            storageSection.visible
                    }

                } // ColumnLayout
            } // QGCFlickable
        } // Item
    }



}
