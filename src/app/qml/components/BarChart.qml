import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Bars with optional dots on a second scale, a small number above each bar,
// and labels underneath. Null dots are left out.
ColumnLayout {
    id: chart

    property var values: []
    property var labels: []
    property var topLabels: []   // strings over the bars, "" for none
    property var dots: []        // second series, or [] for none
    property real dotMax: 10
    property color barColor: Kirigami.Theme.highlightColor
    property color dotColor: "#f59e0b"
    property var tooltips: []    // text per bar
    property real chartHeight: Kirigami.Units.gridUnit * 7
    property real labelWidth: Kirigami.Units.gridUnit * 3 // room each label needs

    readonly property real maxValue: Math.max(1, ...values)
    readonly property int count: values.length
    // Every label when they fit, otherwise every second, third…
    readonly property int labelStep: Math.max(1, Math.ceil(count * labelWidth / Math.max(1, width)))

    spacing: Kirigami.Units.smallSpacing

    Item {
        id: plot
        Layout.fillWidth: true
        Layout.preferredHeight: chart.chartHeight
        readonly property real slot: width / Math.max(1, chart.count)
        readonly property real headroom: Kirigami.Units.gridUnit // room for the numbers above

        Repeater {
            model: [0.5, 1.0]
            Kirigami.Separator {
                required property real modelData
                width: plot.width
                y: plot.headroom + (plot.height - plot.headroom) * (1 - modelData)
                opacity: 0.4
            }
        }
        Repeater {
            model: chart.count
            Item {
                id: bar
                required property int index
                readonly property real value: chart.values[index] || 0
                readonly property var dot: chart.dots.length > index ? chart.dots[index] : null
                x: index * plot.slot
                width: plot.slot
                height: plot.height

                Rectangle {
                    id: fill
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    width: Math.max(2, parent.width * 0.6)
                    height: (plot.height - plot.headroom) * bar.value / chart.maxValue
                    radius: Math.min(width / 2, Kirigami.Units.cornerRadius)
                    color: chart.barColor
                    opacity: hover.hovered ? 1 : 0.75
                }
                Controls.Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: fill.top
                    text: chart.topLabels[bar.index] || ""
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                    visible: text !== "" && parent.width >= Kirigami.Units.gridUnit * 0.8
                }
                Rectangle {
                    visible: bar.dot !== null && bar.dot !== undefined
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: visible ? plot.headroom + (plot.height - plot.headroom) * (1 - bar.dot / chart.dotMax) - height / 2 : 0
                    width: Kirigami.Units.smallSpacing * 2.5
                    height: width
                    radius: width / 2
                    color: chart.dotColor
                    border.color: Kirigami.Theme.backgroundColor
                    border.width: 2
                }
                HoverHandler { id: hover }
                Controls.ToolTip.visible: hover.hovered && (chart.tooltips[bar.index] || "") !== ""
                Controls.ToolTip.text: chart.tooltips[bar.index] || ""
                Controls.ToolTip.delay: Kirigami.Units.toolTipDelay
            }
        }
    }
    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: Kirigami.Units.gridUnit
        Repeater {
            model: chart.count
            Controls.Label {
                required property int index
                // Count from the end, so the latest bar is always labelled.
                visible: (chart.count - 1 - index) % chart.labelStep === 0
                x: index * plot.slot + (plot.slot - width) / 2
                text: chart.labels[index] || ""
                font: Kirigami.Theme.smallFont
                color: Kirigami.Theme.disabledTextColor
            }
        }
    }
}
