/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick                  2.3
import QtQuick.Controls         1.2
import QtQuick.Controls.Styles  1.4

import QGroundControl               1.0
import QGroundControl.ScreenTools   1.0
import QGroundControl.Palette       1.0

Item {
    id:         _root
    visible:    false

    signal          clicked()
    property real   radius:             ScreenTools.isMobile ? ScreenTools.defaultFontPixelHeight * 1.75 : ScreenTools.defaultFontPixelHeight * 1.25
    property real   viewportMargins:    0
    property var    toolStrip

    // Should be an enum but that get's into the whole problem of creating a singleton which isn't worth the effort
    readonly property int dropLeft:     1
    readonly property int dropRight:    2
    readonly property int dropUp:       3
    readonly property int dropDown:     4

    // Arrow is effectively removed by making its width 0
    readonly property real _arrowBaseHeight:    radius
    readonly property real _arrowPointWidth:    0
    readonly property real _dropMargin:         ScreenTools.defaultFontPixelWidth * 0.6

    property var    _dropEdgeTopPoint
    property alias  _dropDownComponent: panelLoader.sourceComponent
    property real   _viewportMaxTop:    0
    property real   _viewportMaxBottom: parent.parent.height - parent.y
    property real   _viewportMaxHeight: _viewportMaxBottom - _viewportMaxTop
    property var    _dropPanelCancel
    property var    _parentButton

    QGCPalette { id: qgcPal }

    // --- helpers for visual size of the loaded panel (logical width/height * its own scale) ---
    function panelVisualWidth() {
        if (!panelLoader.item)
            return 0
        var s = (panelLoader.item.scale !== undefined && panelLoader.item.scale !== null)
                ? panelLoader.item.scale : 1
        return panelLoader.item.width * s
    }

    function panelVisualHeight() {
        if (!panelLoader.item)
            return 0
        var s = (panelLoader.item.scale !== undefined && panelLoader.item.scale !== null)
                ? panelLoader.item.scale : 1
        return panelLoader.item.height * s
    }
    // ---------------------------------------------------------------------

    function show(panelEdgeTopPoint, panelComponent, parentButton) {
        _parentButton     = parentButton
        _dropEdgeTopPoint = panelEdgeTopPoint
        _dropDownComponent = panelComponent
        _calcPositions()
        visible = true
        _dropPanelCancel = dropPanelCancelComponent.createObject(toolStrip.parent)
    }

    function hide() {
        if (_dropPanelCancel) {
            _dropPanelCancel.destroy()
            _parentButton.checked = false
            visible = false
            _dropDownComponent = undefined
        }
    }

    function _calcPositions() {
        if (!panelLoader.item)
            return

        var panelComponentWidth  = panelLoader.item.width
        var panelComponentHeight = panelLoader.item.height

        // Desired *visual* size (after all scaling) = content + margins
        var rawWidth  = panelComponentWidth  + (_dropMargin * 2)
        var rawHeight = panelComponentHeight + (_dropMargin * 2)

        // Compensate ToolStrip scaling so background visually matches contents
        var s = (toolStrip && toolStrip.scaleFactor) ? toolStrip.scaleFactor : 1.0

        // In local coords of the scaled ToolStrip, we shrink width by 1/s
        dropDownItem.width  = rawWidth  / s
        dropDownItem.height = rawHeight

        // --- VIEWPORT CLAMPING (height) ---
        var maxAllowedHeight = _viewportMaxHeight
        if (dropDownItem.height > maxAllowedHeight) {
            dropDownItem.height = maxAllowedHeight
        }

        // --- HORIZONTAL POSITION: drop to the right of the buttons (same as before) ---
        dropDownItem.x = _dropEdgeTopPoint.x + _dropMargin

        // --- VERTICAL POSITION: bottom of panel ~ center of button, clamped to viewport ---
        // Treat _dropEdgeTopPoint.y as the desired bottom anchor in parent coords
        var desiredBottom = _dropEdgeTopPoint.y
        var desiredTop    = desiredBottom - dropDownItem.height

        // Clamp vertically to viewport
        if (desiredTop < _viewportMaxTop) {
            desiredTop    = _viewportMaxTop
            desiredBottom = desiredTop + dropDownItem.height
        }
        if (desiredBottom > _viewportMaxBottom) {
            desiredBottom = _viewportMaxBottom
            desiredTop    = desiredBottom - dropDownItem.height
        }

        dropDownItem.y = desiredTop

        // Arrow points (no real arrow now, but keep for compatibility)
        arrowCanvas.arrowPoint.y = (_dropEdgeTopPoint.y) - dropDownItem.y
        arrowCanvas.arrowPoint.x = 0
        arrowCanvas.arrowBase1.x = _arrowPointWidth
        arrowCanvas.arrowBase1.y = arrowCanvas.arrowPoint.y - (_arrowBaseHeight / 2)
        arrowCanvas.arrowBase2.x = arrowCanvas.arrowBase1.x
        arrowCanvas.arrowBase2.y = arrowCanvas.arrowBase1.y + _arrowBaseHeight
        arrowCanvas.requestPaint()
    }

    Component {
        // Overlay which is used to cancel the panel when the user clicks away
        id: dropPanelCancelComponent

        MouseArea {
            anchors.fill:   parent
            z:              toolStrip.z - 1
            onClicked:      dropPanel.hide()
        }
    }

    // This item is sized to hold the entirety of the drop panel (no arrow triangle anymore)
    Item {
        id: dropDownItem

        DeadMouseArea {
            anchors.fill: parent
        }

        Canvas {
            id:             arrowCanvas
            anchors.fill:   parent

            property point arrowPoint: Qt.point(0, 0)
            property point arrowBase1: Qt.point(0, 0)
            property point arrowBase2: Qt.point(0, 0)

            onPaint: {
                var panelX      = 0
                var panelY      = 0
                var panelWidth  = parent.width
                var panelHeight = parent.height

                var context = getContext("2d")
                context.reset()
                context.beginPath()

                // Simple rect background, no arrow
                context.moveTo(panelX,               panelY)
                context.lineTo(panelX + panelWidth,  panelY)
                context.lineTo(panelX + panelWidth,  panelY + panelHeight)
                context.lineTo(panelX,               panelY + panelHeight)
                context.closePath()

                context.fillStyle = qgcPal.windowShade
                context.fill()
            }
        } // Canvas - arrowCanvas

        QGCFlickable {
            id:                 panelItemFlickable
            anchors.margins:    _dropMargin
            anchors.leftMargin: _dropMargin   // no arrow offset any more
            anchors.fill:       parent
            flickableDirection: Flickable.VerticalFlick

            // Use visual size of content for scroll area
            contentWidth:       panelVisualWidth()
            contentHeight:      panelVisualHeight()

            Loader {
                id: panelLoader

                onHeightChanged:    _calcPositions()
                onWidthChanged:     _calcPositions()

                property var dropPanel: _root
            }
        }
    } // Item - dropDownItem
}
