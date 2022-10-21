import QtQuick                  2.3
import QtQuick.Controls         1.2
import QtQuick.Controls.Styles  1.4
import QtQuick.Dialogs          1.2
import QtQuick.Layouts          1.2
//import QtWebEngine              1.8

import QGroundControl                       1.0
import QGroundControl.FactSystem            1.0
import QGroundControl.FactControls          1.0
import QGroundControl.Controls              1.0
import QGroundControl.ScreenTools           1.0
import QGroundControl.MultiVehicleManager   1.0
import QGroundControl.Palette               1.0

Rectangle {
    id: __geoServerRoot
    color: qgcPal.window
    anchors.fill: parent

    property real _labelWidth:          ScreenTools.defaultFontPixelWidth * 28
    property real _valueWidth:          ScreenTools.defaultFontPixelWidth * 24
    property int  _selectedCount:       0
    property real _columnSpacing:       ScreenTools.defaultFontPixelHeight * 0.25
    property var loadRequestStatus

    QGCPalette { id: qgcPal}

    QGCFlickable {
        clip:               true
        anchors.fill:       parent
        anchors.margins:    ScreenTools.defaultFontPixelWidth
        contentHeight:      settingsColumn.height
        contentWidth:       settingsColumn.width
        flickableDirection: Flickable.VerticalFlick

        Column {
            id:                 settingsColumn
            width:              __geoServerRoot.width
            spacing:            ScreenTools.defaultFontPixelHeight * 0.5
            anchors.margins:    ScreenTools.defaultFontPixelWidth
//            //-----------------------------------------------------------------
//            //-- Geoserver Web View

//            Column
//            {
//                id:         webViewColumn
//                width:      __geoServerRoot.width * 0.8
//                spacing:    ScreenTools.defaultFontPixelHeight * 0.25
//                anchors.horizontalCenter: parent.horizontalCenter
//                Item {
//                    width:              parent.width
//                    height:             gwiLabel.height
//                    anchors.margins:    ScreenTools.defaultFontPixelWidth
//                    QGCLabel {
//                        id:             gwiLabel
//                        text:           qsTr("Geoserver Web Interface")
//                        font.family:    ScreenTools.demiboldFontFamily
//                    }
//                }

//                Rectangle {
//                    height: ScreenTools.isMobile ? 300 : 700
//                    width:  parent.width
//                    color:  qgcPal.windowShade

//                    WebEngineView {
//                        id: webView
//                        anchors.fill: parent
//                        url: "http://localhost:8080/geoserver/web/"
//                        onLoadingChanged: __geoServerRoot.loadRequestStatus = loadRequest.status
//                    }
//                }
//            }

            Column
            {
                id: visualMapSettingsColumn
                width:      __geoServerRoot.width * 0.8
                spacing:    ScreenTools.defaultFontPixelHeight * 0.25
                anchors.horizontalCenter: parent.horizontalCenter

                Item {
                    width:              parent.width
                    height:             vmLabel.height
                    anchors.margins:    ScreenTools.defaultFontPixelWidth
                    QGCLabel {
                        id:             vmLabel
                        text:           qsTr("Visual map settings")
                        font.family:    ScreenTools.demiboldFontFamily
                    }
                }
                Rectangle {
                    height: visualMapValuesColumn.height + ScreenTools.defaultFontPixelHeight * 3
                    width:  parent.width
                    color:  qgcPal.windowShade
                    anchors.margins: ScreenTools.defaultFontPixelWidth
                    Column
                    {
                        id: visualMapValuesColumn
                        spacing:    ScreenTools.defaultFontPixelHeight * 0.25
                        anchors.centerIn: parent

                        Row {
                            spacing:    ScreenTools.defaultFontPixelWidth
                            QGCLabel {
                                width:              _labelWidth
                                text:              qsTr("Visual WMTS Version:")
                            }
                            FactComboBox {
                                id:     visualWMTSVersionField
                                width:  _valueWidth
                                fact:   QGroundControl.settingsManager.geoserverSettings.visualWMTSVersion
                            }
                        }

                        Row {
                            spacing:    ScreenTools.defaultFontPixelWidth
                            QGCLabel {
                                width:              _labelWidth
                                text:              qsTr("Visual Tile Image Format:")
                            }
                            FactComboBox {
                                id:     visualImageFormatField
                                width:  _valueWidth
                                fact:   QGroundControl.settingsManager.geoserverSettings.visualImageFormat
                            }
                        }

                        Row {
                            spacing:    ScreenTools.defaultFontPixelWidth
                            QGCLabel {
                                width:              _labelWidth
                                anchors.baseline:   visualMapWorkspaceField.baseline
                                text:               qsTr("Visual Map Workspace:")
                            }
                            FactTextField {
                                id:     visualMapWorkspaceField
                                width:  _valueWidth
                                fact:   QGroundControl.settingsManager.geoserverSettings.visualMapWorkspace
                            }
                        }

                        Row {
                            spacing:    ScreenTools.defaultFontPixelWidth
                            QGCLabel {
                                width:              _labelWidth
                                anchors.baseline:   visualMapStoreField.baseline
                                text:              qsTr("Visual Map Store:")
                            }
                            FactTextField {
                                id:     visualMapStoreField
                                width:  _valueWidth
                                fact:   QGroundControl.settingsManager.geoserverSettings.visualMapName
                            }
                        }
                        Row {
                            spacing:    ScreenTools.defaultFontPixelWidth
                            QGCLabel {
                                width:              _labelWidth
                                anchors.baseline:   epsgNumberField.baseline
                                text:              qsTr("Visual EPSG Number:")
                            }
                            FactTextField {
                                id:     epsgNumberField
                                width:  _valueWidth
                                fact:   QGroundControl.settingsManager.geoserverSettings.visualEPSGNumber
                            }
                        }
                    }
                }
            }

            Column
            {
                id: elevationMapSettingsColumn
                width:      __geoServerRoot.width * 0.8
                spacing:    ScreenTools.defaultFontPixelHeight * 0.25
                anchors.horizontalCenter: parent.horizontalCenter

                Item {
                    width:              parent.width
                    height:             vmLabel.height
                    anchors.margins:    ScreenTools.defaultFontPixelWidth
                    QGCLabel {
                        id:             emLabel
                        text:           qsTr("Elevation map settings")
                        font.family:    ScreenTools.demiboldFontFamily
                    }
                }

                Rectangle {
                    height: elevationMapValuesColumn.height + ScreenTools.defaultFontPixelHeight * 3
                    width:  parent.width
                    color:  qgcPal.windowShade
                    anchors.margins: ScreenTools.defaultFontPixelWidth
                    Column
                    {
                        id: elevationMapValuesColumn
                        spacing:    ScreenTools.defaultFontPixelHeight * 0.25
                        anchors.centerIn: parent

                        Row {
                            spacing:    ScreenTools.defaultFontPixelWidth
                            QGCLabel {
                                width:              _labelWidth
                                text:              qsTr("Elevation WMTS Version:")
                            }
                            FactComboBox {
                                id:     elevationWMTSVersionField
                                width:  _valueWidth
                                fact:   QGroundControl.settingsManager.geoserverSettings.elevationWMTSVersion
                            }
                        }

                        Row {
                            spacing:    ScreenTools.defaultFontPixelWidth
                            QGCLabel {
                                width:              _labelWidth
                                anchors.baseline:   elevationMapWorkspaceField.baseline
                                text:               qsTr("Elevation Map Workspace:")
                            }
                            FactTextField {
                                id:     elevationMapWorkspaceField
                                width:  _valueWidth
                                fact:   QGroundControl.settingsManager.geoserverSettings.elevationMapWorkspace
                            }
                        }

                        Row {
                            spacing:    ScreenTools.defaultFontPixelWidth
                            QGCLabel {
                                width:              _labelWidth
                                anchors.baseline:   elevationMapStoreField.baseline
                                text:              qsTr("Elevation Map Store:")
                            }
                            FactTextField {
                                id:     elevationMapStoreField
                                width:  _valueWidth
                                fact:   QGroundControl.settingsManager.geoserverSettings.elevationMapName
                            }
                        }
                        Row {
                            spacing:    ScreenTools.defaultFontPixelWidth
                            QGCLabel {
                                width:              _labelWidth
                                anchors.baseline:   elevationEPSGNumber.baseline
                                text:              qsTr("Elevation EPSG number:")
                            }
                            FactTextField {
                                id:     elevationEPSGNumber
                                width:  _valueWidth
                                fact:   QGroundControl.settingsManager.geoserverSettings.elevationEPSGNumber
                            }
                        }
                    }
                }
            }
        }
    }
}
