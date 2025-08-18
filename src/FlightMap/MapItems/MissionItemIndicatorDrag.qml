/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick      2.3
import QtLocation   5.3

import QGroundControl               1.0
import QGroundControl.ScreenTools   1.0
import QGroundControl.Controls      1.0

import QGroundControl.Controllers   1.0
import QGroundControl.FactSystem    1.0

/// Use to drag a MissionItemIndicator
Rectangle {
    id:             itemDragger
    x:              _itemIndicatorX - _touchMarginHorizontal
    y:              _itemIndicatorY - _touchMarginVertical
    width:          _itemIndicatorWidth + (_touchMarginHorizontal * 2)
    height:         _itemIndicatorHeight + (_touchMarginVertical * 2)
    color:          "transparent"
    z:              QGroundControl.zOrderMapItems + 1    // Above item icons

    // Properties which must be specified by consumer
    property var mapControl     ///< Map control which contains this item
    property var itemIndicator  ///< The mission item indicator to drag around
    property var itemCoordinate ///< Coordinate we are updating during drag

    signal clicked
    signal dragStart
    signal dragStop

    property bool   _preventCoordinateBindingLoop:  false

    property real _itemIndicatorX:          itemIndicator ? itemIndicator.x : 0
    property real _itemIndicatorY:          itemIndicator ? itemIndicator.y : 0
    property real _itemIndicatorWidth:      itemIndicator ? itemIndicator.width : 0
    property real _itemIndicatorHeight:     itemIndicator ? itemIndicator.height : 0
    property bool _mobile:                  ScreenTools.isMobile
    property real _touchWidth:              Math.max(_itemIndicatorWidth, ScreenTools.minTouchPixels)
    property real _touchHeight:             Math.max(_itemIndicatorHeight, ScreenTools.minTouchPixels)
    property real _touchMarginHorizontal:   _mobile ? (_touchWidth - _itemIndicatorWidth) / 2 : 0
    property real _touchMarginVertical:     _mobile ? (_touchHeight - _itemIndicatorHeight) / 2 : 0
    property bool _dragStartSignalled:      false

    onXChanged: liveDrag()
    onYChanged: liveDrag()

    function liveDrag() {
        if (!itemDragger._preventCoordinateBindingLoop && itemDrag.drag.active) {
            var point = Qt.point(
                itemDragger.x + _touchMarginHorizontal + (itemIndicator ? itemIndicator.anchorPoint.x : 0),
                itemDragger.y + _touchMarginVertical   + (itemIndicator ? itemIndicator.anchorPoint.y : 0)
            )
            var coordinate = mapControl.toCoordinate(point, false /* clipToViewPort */)
            itemDragger._preventCoordinateBindingLoop = true
            coordinate.altitude = itemCoordinate ? itemCoordinate.altitude : 0
            itemCoordinate = coordinate
            globals.dragCoordinate = coordinate     // keep latest coord while dragging
            itemDragger._preventCoordinateBindingLoop = false
        }
    }

    Drag.active: itemDrag.drag.active

    QGCMouseArea {
        id:                 itemDrag
        anchors.fill:       parent
        drag.target:        parent
        drag.minimumX:      0
        drag.minimumY:      0
        // keep this guard to avoid "width of null"
        drag.maximumX:      itemDragger.parent ? (itemDragger.parent.width  - parent.width)  : 0
        drag.maximumY:      itemDragger.parent ? (itemDragger.parent.height - parent.height) : 0
        preventStealing:    true
        enabled:            itemDragger.visible

        onClicked: {
            focus = true
            itemDragger.clicked()
        }

        // >>> NEW: drive globals.dragActive only from mouse press/release <<<
        onPressed: {
            mouse.accepted = true
            focus = true
            if (!itemDragger._dragStartSignalled) {
                itemDragger._dragStartSignalled = true
                itemDragger.dragStart()
            }
            globals.dragActive = true
        }

        onPositionChanged: mouse.accepted = true

        onReleased: {
            mouse.accepted = true
            itemDragger._dragStartSignalled = false
            itemDragger.dragStop()
            globals.dragActive = false
            globals.dragCoordinate = itemCoordinate
        }
        // <<< END NEW >>>

        // keep this only for internal visuals; DO NOT touch globals here
        property bool dragActive: drag.active
        onDragActiveChanged: {
            if (dragActive) {
                focus = true
                if (!itemDragger._dragStartSignalled) {
                    itemDragger._dragStartSignalled = true
                    itemDragger.dragStart()
                }
            }
            // intentionally no 'else' — prevents false releases on fast moves
        }
    }
}
