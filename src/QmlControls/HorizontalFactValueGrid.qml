/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick          2.12
import QtQuick.Layouts  1.2
import QtQuick.Controls 2.5
import QtQml            2.12

import QGroundControl.Templates     1.0 as T
import QGroundControl.Controls      1.0
import QGroundControl.ScreenTools   1.0
import QGroundControl.Controllers   1.0
import QGroundControl.Palette       1.0
import QGroundControl.FlightMap     1.0
import QGroundControl               1.0

// Note: This control will spit out qWarnings like this: "QGridLayoutEngine::addItem: Cell (0, 1) already taken"
// This is due to Qt bug https://bugreports.qt.io/browse/QTBUG-65121
// If this becomes a problem I'll implement our own grid layout control

T.HorizontalFactValueGrid {
    id:                     _root
    Layout.preferredWidth:  topLayout.width
    Layout.preferredHeight: topLayout.height
    Layout.fillWidth:       true

    property bool   settingsUnlocked:       false

    property real   _margins:               ScreenTools.defaultFontPixelWidth / 2
    property int    _rowMax:                2
    property real   _rowButtonWidth:        ScreenTools.minTouchPixels
    property real   _rowButtonHeight:       ScreenTools.minTouchPixels / 2
    property real   _editButtonSpacing:     2

    QGCPalette { id: qgcPal; colorGroupEnabled: enabled }

    ColumnLayout {
        id:         topLayout
        spacing:    0

        RowLayout {
            Layout.fillWidth:   true

            RowLayout {
                id:             labelValueColumnLayout
                spacing:        ScreenTools.defaultFontPixelWidth * 1.25
                Layout.fillWidth: true

                Repeater {
                    model: _root.columns

                    GridLayout {
                        id:             columnGrid
                        rows:           object.count
                        columns:        2
                        rowSpacing:     0
                        columnSpacing:  ScreenTools.defaultFontPixelWidth / 4
                        flow:           GridLayout.TopToBottom
                        Layout.fillWidth: true

                        // baseline width; we reduce it by 10%
                        property real widthFactor:   0.7
                        property int  valueBasePx:   ScreenTools.defaultFontPixelWidth * 20
                        property real valueMinWidth: valueBasePx * widthFactor

                        Repeater {
                            id:     labelRepeater
                            model:  object

                            InstrumentValueLabel {
                                Layout.fillHeight:  true
                                Layout.alignment:   Qt.AlignHCenter | Qt.AlignVCenter   // centered
                                instrumentValueData: object
                            }
                        }

                        Repeater {
                            id:     valueRepeater
                            model:  object

                            property real   _index:     index
                            property real   maxWidth:   0       // content-driven max
                            property var    lastCheck:  new Date().getTime()

                            function recalcWidth() {
                                var newMaxWidth = 0
                                for (var i = 0; i < valueRepeater.count; i++) {
                                    var itm = valueRepeater.itemAt(i)
                                    if (itm) newMaxWidth = Math.max(newMaxWidth, itm.contentWidth)
                                }
                                maxWidth = Math.max(maxWidth, newMaxWidth)
                            }

                            InstrumentValueValue {
                                Layout.fillHeight:      true
                                Layout.alignment:       Qt.AlignHCenter | Qt.AlignVCenter   // centered
                                Layout.fillWidth:       true

                                // Keep respecting content width, but use a 10% smaller baseline
                                // target = max(content width seen, baseline)
                                readonly property real targetWidth: Math.max(valueRepeater.maxWidth, columnGrid.valueMinWidth)

                                Layout.minimumWidth:    columnGrid.valueMinWidth
                                Layout.preferredWidth:  targetWidth * columnGrid.widthFactor

                                instrumentValueData:    object

                                property real lastContentWidth

                                Component.onCompleted:  {
                                    valueRepeater.maxWidth = Math.max(valueRepeater.maxWidth, contentWidth)
                                    lastContentWidth = contentWidth
                                }

                                onContentWidthChanged: {
                                    valueRepeater.maxWidth = Math.max(valueRepeater.maxWidth, contentWidth)
                                    lastContentWidth = contentWidth
                                    var currentTime = new Date().getTime()
                                    if (currentTime - valueRepeater.lastCheck > 30 * 1000) {
                                        valueRepeater.lastCheck = currentTime
                                        valueRepeater.recalcWidth()
                                    }
                                }
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.bottomMargin:    1
                Layout.fillHeight:      true
                Layout.preferredWidth:  ScreenTools.minTouchPixels / 2
                spacing:                1
                visible:                settingsUnlocked
                enabled:                settingsUnlocked

                QGCButton {
                    Layout.fillHeight:      true
                    Layout.preferredHeight: ScreenTools.minTouchPixels
                    Layout.preferredWidth:  parent.width
                    text:                   qsTr("+")
                    enabled: _root.columns.count < 3
                    onClicked: {
                        if (_root.columns.count < 3) {     // *** limit columns to 3
                            appendColumn()
                        } else {
                            qgcApp.showAppMessage(qsTr("Maximum of 3 columns reached"))
                        }
                    }
                }

                QGCButton {
                    Layout.fillHeight:      true
                    Layout.preferredHeight: ScreenTools.minTouchPixels
                    Layout.preferredWidth:  parent.width
                    text:                   qsTr("-")
                    enabled:                _root.columns.count > 1
                    onClicked:              deleteLastColumn()
                }
            }
        }

        RowLayout {
            Layout.preferredHeight: ScreenTools.minTouchPixels / 2
            Layout.fillWidth:       true
            spacing:                1
            visible:                settingsUnlocked
            enabled:                settingsUnlocked

            QGCButton {
                Layout.fillWidth:       true
                Layout.preferredHeight: parent.height
                text:                   qsTr("+")
                enabled: _root.rowCount < 5
                onClicked: {
                    if (_root.rowCount < 5) {          // *** limit rows to 5
                        appendRow()
                    } else {
                        qgcApp.showAppMessage(qsTr("Maximum of 5 rows reached"))
                    }
                }
            }

            QGCButton {
                Layout.fillWidth:       true
                Layout.preferredHeight: parent.height
                text:                   qsTr("-")
                enabled:                _root.rowCount > 1
                onClicked:              deleteLastRow()
            }
        }
    }

    QGCMouseArea {
        x:          labelValueColumnLayout.x
        y:          labelValueColumnLayout.y
        width:      labelValueColumnLayout.width
        height:     labelValueColumnLayout.height
        visible:    settingsUnlocked
        cursorShape:Qt.PointingHandCursor

        property var mappedLabelValueColumnLayoutPosition: _root.mapFromItem(labelValueColumnLayout, labelValueColumnLayout.x, labelValueColumnLayout.y)

        onClicked: {
            var columnGridLayoutItem = labelValueColumnLayout.childAt(mouse.x, mouse.y)
            var mappedMouse = labelValueColumnLayout.mapToItem(columnGridLayoutItem, mouse.x, mouse.y)
            var labelOrDataItem = columnGridLayoutItem.childAt(mappedMouse.x, mappedMouse.y)
            if (labelOrDataItem && labelOrDataItem.instrumentValueData !== undefined) {
                mainWindow.showPopupDialogFromComponent(valueEditDialog, { instrumentValueData: labelOrDataItem.instrumentValueData })
            }
        }
    }

    Component {
        id: valueEditDialog
        InstrumentValueEditDialog { }
    }
}
