/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick                      2.11
import QtQuick.Controls             2.4
import QtLocation                   5.3
import QtPositioning                5.3
import QtQuick.Dialogs              1.2

import QGroundControl               1.0
import QGroundControl.Airspace      1.0
import QGroundControl.Controllers   1.0
import QGroundControl.Controls      1.0
import QGroundControl.FlightDisplay 1.0
import QGroundControl.FlightMap     1.0
import QGroundControl.Palette       1.0
import QGroundControl.ScreenTools   1.0
import QGroundControl.Vehicle       1.0



import QtQuick.Layouts  1.2
import QtQuick.Window   2.2
import QtGraphicalEffects 1.12
import QGroundControl.FactSystem        1.0
import QGroundControl.FactControls      1.0
import QGroundControl.ShapeFileHelper   1.0
import QGroundControl.Airmap            1.0

import io.qt.examples.backend           1.0

FlightMap {
    id:                         _root
    allowGCSLocationCenter:     true
    allowVehicleLocationCenter: !_keepVehicleCentered
    planView:                   false
    zoomLevel:                  QGroundControl.flightMapZoom
    center:                     QGroundControl.flightMapPosition

    property Item pipState: _pipState
    QGCPipState {
        id:         _pipState
        pipOverlay: _pipOverlay
        isDark:     _isFullWindowItemDark
    }

    property var    rightPanelWidth
    property var    planMasterController
    property bool   pipMode:                    false   // true: map is shown in a small pip mode
    property var    toolInsets                          // Insets for the center viewport area

    property var    _activeVehicle:             QGroundControl.multiVehicleManager.activeVehicle
    property var    _planMasterController:      planMasterController
    property var    _geoFenceController:        planMasterController.geoFenceController
    property var    _rallyPointController:      planMasterController.rallyPointController
    property var    _activeVehicleCoordinate:   _activeVehicle ? _activeVehicle.coordinate : QtPositioning.coordinate()
    property real   _toolButtonTopMargin:       parent.height - mainWindow.height + (ScreenTools.defaultFontPixelHeight / 2)
    property real   _toolsMargin:               ScreenTools.defaultFontPixelWidth * 0.75
    property bool   _airspaceEnabled:           QGroundControl.airmapSupported ? (QGroundControl.settingsManager.airMapSettings.enableAirMap.rawValue && QGroundControl.airspaceManager.connected): false
    property var    _flyViewSettings:           QGroundControl.settingsManager.flyViewSettings
    property bool   _keepMapCenteredOnVehicle:  _flyViewSettings.keepMapCenteredOnVehicle.rawValue

    property var    historyItemData

    property bool   _disableVehicleTracking:    false
    property bool   _keepVehicleCentered:       pipMode ? true : false
    property bool   _saveZoomLevelSetting:      true

    function updateAirspace(reset) {
        if(_airspaceEnabled) {
            var coordinateNW = _root.toCoordinate(Qt.point(0,0), false /* clipToViewPort */)
            var coordinateSE = _root.toCoordinate(Qt.point(width,height), false /* clipToViewPort */)
            if(coordinateNW.isValid && coordinateSE.isValid) {
                QGroundControl.airspaceManager.setROI(coordinateNW, coordinateSE, false /*planView*/, reset)
            }
        }
    }

    function _adjustMapZoomForPipMode() {
        _saveZoomLevelSetting = false
        if (pipMode) {
            if (QGroundControl.flightMapZoom > 3) {
                zoomLevel = QGroundControl.flightMapZoom - 3
            }
        } else {
            zoomLevel = QGroundControl.flightMapZoom
        }
        _saveZoomLevelSetting = true
    }

    onPipModeChanged: _adjustMapZoomForPipMode()

    onVisibleChanged: {
        if (visible) {
            // Synchronize center position with Plan View
            center = QGroundControl.flightMapPosition
        }
    }

    onZoomLevelChanged: {
        if (_saveZoomLevelSetting) {
            QGroundControl.flightMapZoom = zoomLevel
            updateAirspace(false)
        }
    }
    onCenterChanged: {
        QGroundControl.flightMapPosition = center
        updateAirspace(false)
    }

    on_AirspaceEnabledChanged: {
        updateAirspace(true)
    }

    // We track whether the user has panned or not to correctly handle automatic map positioning
    Connections {
        target: gesture

        function onPanStarted() {       _disableVehicleTracking = true }
        function onFlickStarted() {     _disableVehicleTracking = true }
        function onPanFinished() {      panRecenterTimer.restart() }
        function onFlickFinished() {    panRecenterTimer.restart() }
    }

    function pointInRect(point, rect) {
        return point.x > rect.x &&
                point.x < rect.x + rect.width &&
                point.y > rect.y &&
                point.y < rect.y + rect.height;
    }

    property real _animatedLatitudeStart
    property real _animatedLatitudeStop
    property real _animatedLongitudeStart
    property real _animatedLongitudeStop
    property real animatedLatitude
    property real animatedLongitude

    onAnimatedLatitudeChanged: _root.center = QtPositioning.coordinate(animatedLatitude, animatedLongitude)
    onAnimatedLongitudeChanged: _root.center = QtPositioning.coordinate(animatedLatitude, animatedLongitude)

    NumberAnimation on animatedLatitude { id: animateLat; from: _animatedLatitudeStart; to: _animatedLatitudeStop; duration: 1000 }
    NumberAnimation on animatedLongitude { id: animateLong; from: _animatedLongitudeStart; to: _animatedLongitudeStop; duration: 1000 }

    function animatedMapRecenter(fromCoord, toCoord) {
        _animatedLatitudeStart = fromCoord.latitude
        _animatedLongitudeStart = fromCoord.longitude
        _animatedLatitudeStop = toCoord.latitude
        _animatedLongitudeStop = toCoord.longitude
        animateLat.start()
        animateLong.start()
    }

    function _insetRect() {
        return Qt.rect(toolInsets.leftEdgeCenterInset,
                       toolInsets.topEdgeCenterInset,
                       _root.width - toolInsets.leftEdgeCenterInset - toolInsets.rightEdgeCenterInset,
                       _root.height - toolInsets.topEdgeCenterInset - toolInsets.bottomEdgeCenterInset)
    }

    function recenterNeeded() {
        var vehiclePoint = _root.fromCoordinate(_activeVehicleCoordinate, false /* clipToViewport */)
        var insetRect = _insetRect()
        return !pointInRect(vehiclePoint, insetRect)
    }

    function updateMapToVehiclePosition() {
        if (animateLat.running || animateLong.running) {
            return
        }
        // We let FlightMap handle first vehicle position
        if (!_keepMapCenteredOnVehicle && firstVehiclePositionReceived && _activeVehicleCoordinate.isValid && !_disableVehicleTracking) {
            if (_keepVehicleCentered) {
                _root.center = _activeVehicleCoordinate
            } else {
                if (firstVehiclePositionReceived && recenterNeeded()) {
                    // Move the map such that the vehicle is centered within the inset area
                    var vehiclePoint = _root.fromCoordinate(_activeVehicleCoordinate, false /* clipToViewport */)
                    var insetRect = _insetRect()
                    var centerInsetPoint = Qt.point(insetRect.x + insetRect.width / 2, insetRect.y + insetRect.height / 2)
                    var centerOffset = Qt.point((_root.width / 2) - centerInsetPoint.x, (_root.height / 2) - centerInsetPoint.y)
                    var vehicleOffsetPoint = Qt.point(vehiclePoint.x + centerOffset.x, vehiclePoint.y + centerOffset.y)
                    var vehicleOffsetCoord = _root.toCoordinate(vehicleOffsetPoint, false /* clipToViewport */)
                    animatedMapRecenter(_root.center, vehicleOffsetCoord)
                }
            }
        }
    }

    on_ActiveVehicleCoordinateChanged: {
        if (_keepMapCenteredOnVehicle && _activeVehicleCoordinate.isValid && !_disableVehicleTracking) {
            _root.center = _activeVehicleCoordinate
        }
    }

    Timer {
        id:         panRecenterTimer
        interval:   10000
        running:    false
        onTriggered: {
            _disableVehicleTracking = false
            updateMapToVehiclePosition()
        }
    }

    Timer {
        interval:       500
        running:        true
        repeat:         true
        onTriggered:    updateMapToVehiclePosition()
    }

    QGCMapPalette { id: mapPal; lightColors: isSatelliteMap }

    Connections {
        target:                 _missionController
        ignoreUnknownSignals:   true
        function onNewItemsFromVehicle() {
            var visualItems = _missionController.visualItems
            if (visualItems && visualItems.count !== 1) {
                mapFitFunctions.fitMapViewportToMissionItems()
                firstVehiclePositionReceived = true
            }
        }
    }

    MapFitFunctions {
        id:                         mapFitFunctions // The name for this id cannot be changed without breaking references outside of this code. Beware!
        map:                        _root
        usePlannedHomePosition:     false
        planMasterController:       _planMasterController
    }

    ObstacleDistanceOverlayMap {
        id: obstacleDistance
        showText: !pipMode
    }

    // Add trajectory lines to the map
    MapPolyline {
        id:         trajectoryPolyline
        line.width: 3
        line.color: "red"
        z:          QGroundControl.zOrderTrajectoryLines
        visible:    !pipMode

        Connections {
            target:                 QGroundControl.multiVehicleManager
            function onActiveVehicleChanged(activeVehicle) {
                trajectoryPolyline.path = _activeVehicle ? _activeVehicle.trajectoryPoints.list() : []
            }
        }

        Connections {
            target:                 _activeVehicle ? _activeVehicle.trajectoryPoints : null
            onPointAdded:           trajectoryPolyline.addCoordinate(coordinate)
            onUpdateLastPoint:      trajectoryPolyline.replaceCoordinate(trajectoryPolyline.pathLength() - 1, coordinate)
            onPointsCleared:        trajectoryPolyline.path = []
        }
    }

    // Add the vehicles to the map
    MapItemView {
        model: QGroundControl.multiVehicleManager.vehicles
        delegate: VehicleMapItem {
            vehicle:        object
            coordinate:     object.coordinate
            map:            _root
            size:           pipMode ? ScreenTools.defaultFontPixelHeight : ScreenTools.defaultFontPixelHeight * 3
            z:              QGroundControl.zOrderVehicles
        }
    }
    // Add distance sensor view
    MapItemView{
        model: QGroundControl.multiVehicleManager.vehicles
        delegate: ProximityRadarMapView {
            vehicle:        object
            coordinate:     object.coordinate
            map:            _root
            z:              QGroundControl.zOrderVehicles
        }
    }
    // Add ADSB vehicles to the map
    MapItemView {
        model: QGroundControl.adsbVehicleManager.adsbVehicles
        delegate: VehicleMapItem {
            coordinate:     object.coordinate
            altitude:       object.altitude
            callsign:       object.callsign
            heading:        object.heading
            alert:          object.alert
            map:            _root
            z:              QGroundControl.zOrderVehicles
        }
    }

    // Add the items associated with each vehicles flight plan to the map
    Repeater {
        model: QGroundControl.multiVehicleManager.vehicles

        PlanMapItems {
            map:                    _root
            largeMapView:           !pipMode
            planMasterController:   masterController
            vehicle:                _vehicle

            property var _vehicle: object

            PlanMasterController {
                id: masterController
                Component.onCompleted: startStaticActiveVehicle(object)
            }
        }
    }

    MapItemView {
        model: pipMode ? undefined : _missionController.directionArrows

        delegate: MapLineArrow {
            fromCoord:      object ? object.coordinate1 : undefined
            toCoord:        object ? object.coordinate2 : undefined
            arrowPosition:  2
            z:              QGroundControl.zOrderWaypointLines
        }
    }

    // Allow custom builds to add map items
    CustomMapItems {
        map:            _root
        largeMapView:   !pipMode
    }

    GeoFenceMapVisuals {
        map:                    _root
        myGeoFenceController:   _geoFenceController
        interactive:            false
        planView:               false
        homePosition:           _activeVehicle && _activeVehicle.homePosition.isValid ? _activeVehicle.homePosition :  QtPositioning.coordinate()
    }

    // Rally points on map
    MapItemView {
        model: _rallyPointController.points

        delegate: MapQuickItem {
            id:             itemIndicator
            anchorPoint.x:  sourceItem.anchorPointX
            anchorPoint.y:  sourceItem.anchorPointY
            coordinate:     object.coordinate
            z:              QGroundControl.zOrderMapItems

            sourceItem: MissionItemIndexLabel {
                id:         itemIndexLabel
                label:      qsTr("R", "rally point map item label")
            }
        }
    }

    // Camera trigger points
    MapItemView {
        model: _activeVehicle ? _activeVehicle.cameraTriggerPoints : 0

        delegate: CameraTriggerIndicator {
            coordinate:     object.coordinate
            z:              QGroundControl.zOrderTopMost
        }
    }

    // GoTo Location visuals
    MapQuickItem {
        id:             gotoLocationItem
        visible:        false
        z:              QGroundControl.zOrderMapItems
        anchorPoint.x:  sourceItem.anchorPointX
        anchorPoint.y:  sourceItem.anchorPointY
        sourceItem: MissionItemIndexLabel {
            checked:    true
            index:      -1
            label:      qsTr("Go here", "Go to location waypoint")
        }

        property bool inGotoFlightMode: _activeVehicle ? _activeVehicle.flightMode === _activeVehicle.gotoFlightMode : false

        onInGotoFlightModeChanged: {
            if (!inGotoFlightMode && gotoLocationItem.visible) {
                // Hide goto indicator when vehicle falls out of guided mode
                gotoLocationItem.visible = false
            }
        }

        Connections {
            target: QGroundControl.multiVehicleManager
            function onActiveVehicleChanged(activeVehicle) {
                if (!activeVehicle) {
                    gotoLocationItem.visible = false
                }
            }
        }

        function show(coord) {
            gotoLocationItem.coordinate = coord
            gotoLocationItem.visible = true
        }

        function hide() {
            gotoLocationItem.visible = false
        }

        function actionConfirmed() {
            // We leave the indicator visible. The handling for onInGuidedModeChanged will hide it.
        }

        function actionCancelled() {
            hide()
        }
    }

    // Orbit editing visuals
    QGCMapCircleVisuals {
        id:             orbitMapCircle
        mapControl:     parent
        mapCircle:      _mapCircle
        visible:        false

        property alias center:              _mapCircle.center
        property alias clockwiseRotation:   _mapCircle.clockwiseRotation
        readonly property real defaultRadius: 30

        Connections {
            target: QGroundControl.multiVehicleManager
            function onActiveVehicleChanged(activeVehicle) {
                if (!activeVehicle) {
                    orbitMapCircle.visible = false
                }
            }
        }

        function show(coord) {
            _mapCircle.radius.rawValue = defaultRadius
            orbitMapCircle.center = coord
            orbitMapCircle.visible = true
        }

        function hide() {
            orbitMapCircle.visible = false
        }

        function actionConfirmed() {
            // Live orbit status is handled by telemetry so we hide here and telemetry will show again.
            hide()
        }

        function actionCancelled() {
            hide()
        }

        function radius() {
            return _mapCircle.radius.rawValue
        }

        Component.onCompleted: globals.guidedControllerFlyView.orbitMapCircle = orbitMapCircle

        QGCMapCircle {
            id:                 _mapCircle
            interactive:        true
            radius.rawValue:    30
            showRotation:       true
            clockwiseRotation:  true
        }
    }

    // ROI Location visuals
    MapQuickItem {
        id:             roiLocationItem
        visible:        _activeVehicle && _activeVehicle.isROIEnabled
        z:              QGroundControl.zOrderMapItems
        anchorPoint.x:  sourceItem.anchorPointX
        anchorPoint.y:  sourceItem.anchorPointY
        sourceItem: MissionItemIndexLabel {
            checked:    true
            index:      -1
            label:      qsTr("ROI here", "Make this a Region Of Interest")
        }

        //-- Visibilty controlled by actual state
        function show(coord) {
            roiLocationItem.coordinate = coord
        }

        function hide() {
        }

        function actionConfirmed() {
        }

        function actionCancelled() {
        }
    }

    // Orbit telemetry visuals
    QGCMapCircleVisuals {
        id:             orbitTelemetryCircle
        mapControl:     parent
        mapCircle:      _activeVehicle ? _activeVehicle.orbitMapCircle : null
        visible:        _activeVehicle ? _activeVehicle.orbitActive : false
    }

    MapQuickItem {
        id:             orbitCenterIndicator
        anchorPoint.x:  sourceItem.anchorPointX
        anchorPoint.y:  sourceItem.anchorPointY
        coordinate:     _activeVehicle ? _activeVehicle.orbitMapCircle.center : QtPositioning.coordinate()
        visible:        orbitTelemetryCircle.visible

        sourceItem: MissionItemIndexLabel {
            checked:    true
            index:      -1
            label:      qsTr("Orbit", "Orbit waypoint")
        }
    }

    // Handle guided mode clicks
    // MouseArea {
    //     anchors.fill: parent

    //     QGCMenu {
    //         id: clickMenu
    //         property var coord
    //         QGCMenuItem {
    //             text:           qsTr("Go to location")
    //             visible:        globals.guidedControllerFlyView.showGotoLocation

    //             onTriggered: {
    //                 gotoLocationItem.show(clickMenu.coord)
    //                 globals.guidedControllerFlyView.confirmAction(globals.guidedControllerFlyView.actionGoto, clickMenu.coord, gotoLocationItem)
    //             }
    //         }
    //         QGCMenuItem {
    //             text:           qsTr("Orbit at location")
    //             visible:        globals.guidedControllerFlyView.showOrbit

    //             onTriggered: {
    //                 orbitMapCircle.show(clickMenu.coord)
    //                 globals.guidedControllerFlyView.confirmAction(globals.guidedControllerFlyView.actionOrbit, clickMenu.coord, orbitMapCircle)
    //             }
    //         }
    //         QGCMenuItem {
    //             text:           qsTr("ROI at location")
    //             visible:        globals.guidedControllerFlyView.showROI

    //             onTriggered: {
    //                 roiLocationItem.show(clickMenu.coord)
    //                 globals.guidedControllerFlyView.confirmAction(globals.guidedControllerFlyView.actionROI, clickMenu.coord, roiLocationItem)
    //             }
    //         }
    //     }

    //     onClicked: {
    //         if (!globals.guidedControllerFlyView.guidedUIVisible && (globals.guidedControllerFlyView.showGotoLocation || globals.guidedControllerFlyView.showOrbit || globals.guidedControllerFlyView.showROI)) {
    //             orbitMapCircle.hide()
    //             gotoLocationItem.hide()
    //             var clickCoord = _root.toCoordinate(Qt.point(mouse.x, mouse.y), false /* clipToViewPort */)
    //             clickMenu.coord = clickCoord
    //             clickMenu.popup()
    //         }
    //     }
    // }

    // Airspace overlap support
    MapItemView {
        model:              _airspaceEnabled && QGroundControl.settingsManager.airMapSettings.enableAirspace && QGroundControl.airspaceManager.airspaceVisible ? QGroundControl.airspaceManager.airspaces.circles : []
        delegate: MapCircle {
            center:         object.center
            radius:         object.radius
            color:          object.color
            border.color:   object.lineColor
            border.width:   object.lineWidth
        }
    }

    MapItemView {
        model:              _airspaceEnabled && QGroundControl.settingsManager.airMapSettings.enableAirspace && QGroundControl.airspaceManager.airspaceVisible ? QGroundControl.airspaceManager.airspaces.polygons : []
        delegate: MapPolygon {
            path:           object.polygon
            color:          object.color
            border.color:   object.lineColor
            border.width:   object.lineWidth
        }
    }

    // MapItemView {
    //     id: historyDropPoints
    //     model: backend.dropPoints
    //     property var dummyModel : backend.dropPoints
    //     visible: QGroundControl.settingsManager.flightMapSettings.enableHistory.value
    //     delegate: MapQuickItem {
    //         coordinate: QtPositioning.coordinate(historyDropPoints.dummyModel[index].lat, historyDropPoints.dummyModel[index].lon)

    //         sourceItem: Item {
    //             id: pinRoot
    //             property real pinScale: 1.5                                  // <— scale factor
    //             property real pinBase:  ScreenTools.defaultFontPixelHeight * 1.5

    //                 // scale the whole marker (pin + badge + text)
    //             width:  Math.round(pinBase * pinScale)                        // was: ScreenTools.defaultFontPixelHeight * 1.5
    //             height: Math.round(pinBase * pinScale)                        // was: ScreenTools.defaultFontPixelHeight * 1.5

    //             // colors
    //             property color pinColor:  "#FFC107"   // amber
    //             property color pinStroke: "#7A5C00"   // darker outline

    //             // rating 0..5, always finite
    //             property int ratingVal: Math.max(0, Math.min(5, Math.round(
    //                 Number(historyDropPoints.dummyModel[index] && historyDropPoints.dummyModel[index].rating) || 0
    //             )))

    //             // Draw the whole pin + badge in one pass
    //             Canvas {
    //                 anchors.fill: parent
    //                 contextType: "2d"
    //                 renderTarget: Canvas.FramebufferObject

    //                 onPaint: {
    //                     const ctx   = getContext("2d")
    //                     const dpr   = Screen.devicePixelRatio || 1
    //                     const overs = 2.0

    //                     if (ctx.resetTransform) ctx.resetTransform()
    //                     ctx.clearRect(0, 0, width, height)
    //                     ctx.scale(dpr * overs, dpr * overs)

    //                     const w = width  / (dpr * overs)
    //                     const h = height / (dpr * overs)

    //                     // geometry (same as before)
    //                     const cx   = w * 0.5
    //                     const r    = w * 0.34           // bulb radius
    //                     const cy   = r + h * 0.16       // bulb center y
    //                     const tipY = h * 0.98           // sharp tip
    //                     const k    = 1.75               // side curve depth

    //                     ctx.lineJoin = "round"
    //                     ctx.lineCap  = "round"

    //                     // pin body (watertight teardrop)
    //                     ctx.beginPath()
    //                     ctx.moveTo(cx + r, cy)                  // start at rightmost
    //                     ctx.arc(cx, cy, r, 0, Math.PI, true)    // over the top to leftmost
    //                     ctx.quadraticCurveTo(cx - r * 0.30, cy + r * k, cx, tipY)
    //                     ctx.quadraticCurveTo(cx + r * 0.30, cy + r * k, cx + r, cy)
    //                     ctx.closePath()

    //                     ctx.fillStyle = pinRoot.pinColor
    //                     ctx.fill()
    //                     ctx.lineWidth   = Math.max(1, r * 0.10)
    //                     ctx.strokeStyle = pinRoot.pinStroke
    //                     ctx.stroke()

    //                     const badgeR = r * 0.75;                 // was r * 0.46
    //                     ctx.beginPath();
    //                     ctx.arc(cx, cy, badgeR, 0, Math.PI * 2, false);
    //                     ctx.fillStyle = "white";
    //                     ctx.fill();
    //                     ctx.lineWidth   = Math.max(1, r * 0.06);
    //                     ctx.strokeStyle = "black";
    //                     ctx.stroke();

    //                     // --- centered number with explicit width-based centering ---
    //                     const text   = String(pinRoot.ratingVal);
    //                     const fontPx = Math.round(badgeR * 1.40);
    //                     ctx.font = fontPx + "px sans-serif";
    //                     ctx.fillStyle = "black";
    //                     ctx.textBaseline = "middle";   // vertical center
    //                     ctx.textAlign = "left";        // we'll position X manually

    //                     // measure width in current transform (scaled context)
    //                     let tw = 0;
    //                     try { tw = ctx.measureText(text).width || 0; } catch (e) { tw = 0; }

    //                     // small optical vertical nudge
    //                     const yNudge = Math.round(fontPx * 0.06);

    //                     // optional tiny per-digit horizontal tweak (helps "1")
    //                     let xNudge = 0;
    //                     if (text === "1") xNudge = -fontPx * 0.08;

    //                     // center by hand using measured width
    //                     const tx = cx - (tw / 2) + xNudge;
    //                     ctx.fillText(text, tx, cy + yNudge);
    //                 }

    //                 Component.onCompleted: requestPaint()
    //                 onWidthChanged: requestPaint()
    //                 onHeightChanged: requestPaint()
    //             }

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

            // Let MapQuickItem do the positioning (instead of x/y hacks)
            anchorPoint.x: pinRoot.width  / 2
            anchorPoint.y: pinRoot.height

            sourceItem: Item {
                id: pinRoot

                property real pinScale: 1.5*0.7
                property real pinBase: ScreenTools.defaultFontPixelHeight * 1.5

                width:  Math.round(pinBase * pinScale)
                height: Math.round(pinBase * pinScale)

                // rating 0..5, always finite
                property int ratingVal: Math.max(0, Math.min(5, Math.round(
                    Number(historyDropPoints.dummyModel[index] && historyDropPoints.dummyModel[index].rating) || 0
                )))

                // --- The SVG pin ---
                Image {
                    id: pinImg
                    anchors.fill: parent
                    source: "qrc:/qmlimages/GeoTagIcon"    // <- adjust extension if needed
                    smooth: true
                    mipmap: true

                    // Important for SVG: controls rasterization size (prevents odd scaling artifacts)
                    sourceSize.width:  pinRoot.width
                    sourceSize.height: pinRoot.height
                }

                // --- Badge (white circle with outline) ---
                Rectangle {
                    id: badge
                    width:  Math.round(pinRoot.width * 0.50)
                    height: width
                    radius: width / 2

                    // position badge where it visually fits the icon
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

                // If you want click handling back:
                // MouseArea {
                //     anchors.fill: parent
                //     onClicked: {
                //         historyItemData = historyDropPoints.dummyModel[index]
                //         historyItemData.index = index
                //         mainWindow.showPopupDialogFromComponent(historyItemPopUp)
                //     }
                // }
            }
        }

        onModelChanged: {
            console.log("Model updated", model)
            historyDropPoints.dummyModel = backend.dropPoints
        }
    }

    //             // keep your original offset so the tip points to the map coordinate
    //             x: -width / 2
    //             y: -height

    //             // MouseArea {
    //             //     anchors.fill: parent
    //             //     onClicked: {
    //             //         console.log("ICON PRESSED")
    //             //         historyItemData = historyDropPoints.dummyModel[index]
    //             //         historyItemData.index = index
    //             //         mainWindow.showPopupDialogFromComponent(historyItemPopUp)
    //             //     }
    //             // }
    //         }


    //     }

    //     onModelChanged: {
    //         console.log("Model updated", model);
    //         historyDropPoints.dummyModel = backend.dropPoints
    //     }
    // }

    // Component {
    //     id: historyItemPopUp

    //     QGCPopupDialog {
    //         id:         historyItemPopUpDialog
    //         title:      qsTr(historyItemData.label)
    //         buttons:    StandardButton.Close

    //         ColumnLayout {
    //             //spacing: _margins

    //             GridLayout {
    //                 id:     gridLayout
    //                 flow:   GridLayout.TopToBottom
    //                 rows:   3

    //                 QGCLabel {
    //                     text:               qsTr("Drop Point Rating:")
    //                     visible:            true
    //                     //onVisibleChanged:   gridLayout.dynamicRows += visible ? 1 : -1
    //                 }


    //                 QGCLabel {
    //                     text:               "Times used:"
    //                     visible:            true
    //                     //onVisibleChanged:   gridLayout.dynamicRows += visible ? 1 : -1
    //                 }

    //                 QGCLabel {
    //                     text:               "GPS Coodinates:"
    //                     visible: true
    //                 }

    //                 QGCButton {
    //                     text: "Use again"
    //                     enabled: globals.activeVehicle && globals.activeVehicle.coordinate.isValid
    //                     onClicked:{
    //                         if(globals.activeVehicle && globals.activeVehicle.coordinate.isValid){
    //                             historyItemPopUpDialog.hideDialog()
    //                             insertSimpleItemAfterCurrent(QtPositioning.coordinate(historyItemData.lat, historyItemData.lon))
    //                         }
    //                     }
    //                 }


    //                 RowLayout {
    //                     id: ratingRow
    //                     spacing: 8

    //                     RowLayout {
    //                         id: starBar
    //                         property int maxStars: 5
    //                         // Don't rely on a non-notifyable binding; seed once:
    //                         property int value: historyItemData.rating
    //                         readonly property string starIcon: "/InstrumentValueIcons/star-full.svg"

    //                         function setRating(v) {
    //                             var nv = Math.max(0, Math.min(maxStars, v))
    //                             if (value === nv && historyItemData.rating === nv) return

    //                             // 1) update local reactive value so UI changes immediately
    //                             value = nv

    //                             // 2) keep your data + backend in sync
    //                             historyItemData.rating = nv
    //                             rating.text = nv
    //                             backend.changeRating(historyItemData.index, nv)
    //                         }

    //                         // 3) if rating can change from outside (after save/load), resync UI:
    //                         Connections {
    //                             target: backend
    //                             onDropPointsChanged: {
    //                                 // pull fresh value from your item, then reflect locally
    //                                 starBar.value = historyItemData.rating
    //                             }
    //                         }

    //                         spacing: 6

    //                         Repeater {
    //                             model: starBar.maxStars
    //                             delegate: Item {
    //                                 width: 40; height: 40
    //                                 property int starIndex: index + 1

    //                                 QGCColoredImage {
    //                                     anchors.fill: parent
    //                                     source: starBar.starIcon
    //                                     fillMode: Image.PreserveAspectFit
    //                                     // Drive transparency via color alpha (reliably updates)
    //                                     color: starBar.value >= starIndex ? Qt.rgba(0.4, 0.4, 0.4, 0.3) : Qt.rgba(0, 0, 0, 1)
    //                                 }
    //                                 MouseArea {
    //                                     anchors.fill: parent
    //                                     onClicked: starBar.setRating(starIndex)
    //                                     hoverEnabled: true
    //                                 }
    //                             }
    //                         }
    //                     }

    //                     QGCLabel {
    //                         id: rating
    //                         text: historyItemData.rating
    //                         visible: true
    //                     }
    //                 }

    //                 QGCLabel {
    //                     text:               historyItemData.counter
    //                     visible:            true
    //                 }

    //                 QGCLabel {
    //                     text: historyItemData.lat + " " + historyItemData.lon
    //                 }

    //                 QGCButton {
    //                     text: "Remove Drop point"
    //                     onClicked:{
    //                         backend.removeDropPoint(historyItemData.index)
    //                         historyItemPopUpDialog.hideDialog()
    //                     }
    //                 }
    //                 // QGCButton {
    //                 //     text: "Change rating"
    //                 // }

    //             }
    //         }
    //     }
    // }

    MapScale {
        id:                 mapScale
        anchors.margins:    _toolsMargin
        anchors.right:       parent.right
        anchors.top:        parent.top
        mapControl:         _root
        buttonsOnLeft:      false
        visible:            !ScreenTools.isTinyScreen && QGroundControl.corePlugin.options.flyView.showMapScale && mapControl.pipState.state === mapControl.pipState.windowState

        property real centerInset: visible ? parent.height - y : 0
    }

}
