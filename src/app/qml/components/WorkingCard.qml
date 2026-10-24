import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.tasktumbler

// The thing being worked on right now, with a live timer. Big on purpose:
// time is easy to lose track of.
Kirigami.AbstractCard {
    id: card

    signal openTask(string taskId)
    readonly property var working: AppController.working
    property double nowMs: Date.now()

    visible: working.id !== undefined
    Layout.fillWidth: true

    Timer {
        interval: 1000
        repeat: true
        running: card.visible
        triggeredOnStart: true
        onTriggered: card.nowMs = Date.now()
    }

    background: Rectangle {
        radius: Kirigami.Units.cornerRadius * 2
        color: Qt.alpha(Kirigami.Theme.positiveTextColor, 0.10)
        border.color: Qt.alpha(Kirigami.Theme.positiveTextColor, 0.45)
        border.width: 1
    }

    contentItem: RowLayout {
        spacing: Kirigami.Units.largeSpacing * 2

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing
            Controls.Label {
                text: i18n("Working on")
                color: Kirigami.Theme.positiveTextColor
                font: Kirigami.Theme.smallFont
            }
            Kirigami.Heading {
                Layout.fillWidth: true
                level: 2
                text: card.working.title || ""
                wrapMode: Text.Wrap
            }
            Controls.Label {
                text: i18n("Started at %1", card.working.startedAt ? Format.time(card.working.startedAt) : "")
                color: Kirigami.Theme.disabledTextColor
            }
        }

        Controls.Label {
            text: card.working.startedAt ? Format.elapsed(card.nowMs - card.working.startedAt) : ""
            font.pixelSize: Kirigami.Units.gridUnit * 2.2
            font.features: { "tnum": 1 }
            font.weight: Font.Light
        }

        ColumnLayout {
            Controls.Button {
                Layout.fillWidth: true
                text: i18n("Done")
                icon.name: "checkmark"
                onClicked: AppController.setTaskStatus(card.working.taskId, "completed")
            }
            Controls.Button {
                Layout.fillWidth: true
                text: i18n("Pause")
                icon.name: "media-playback-pause"
                onClicked: AppController.stopWork()
            }
        }
    }

    TapHandler {
        onTapped: card.openTask(card.working.taskId)
    }
}
