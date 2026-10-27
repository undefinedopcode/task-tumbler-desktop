import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.tasktumbler

Kirigami.ScrollablePage {
    id: page

    title: i18n("Categories")

    actions: [
        Kirigami.Action {
            text: i18n("New category")
            icon.name: "list-add"
            onTriggered: categoryDialog.edit(null)
        }
    ]

    CategoryDialog { id: categoryDialog }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Controls.Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Kirigami.Theme.disabledTextColor
            text: i18n("Each category has its own hours. A task is only ever scheduled inside its category's hours, so work stays in work time.")
        }

        Repeater {
            model: AppController.categories
            Kirigami.AbstractCard {
                id: card
                required property var modelData
                Layout.fillWidth: true

                contentItem: RowLayout {
                    spacing: Kirigami.Units.largeSpacing
                    Rectangle {
                        Layout.preferredWidth: Kirigami.Units.gridUnit
                        Layout.preferredHeight: Kirigami.Units.gridUnit
                        radius: width / 2
                        color: card.modelData.color
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing
                        RowLayout {
                            Kirigami.Heading {
                                level: 4
                                text: card.modelData.name
                            }
                            Chip {
                                visible: card.modelData.isDefault
                                text: i18n("Default")
                                tint: Kirigami.Theme.highlightColor
                                strong: true
                            }
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing
                            Repeater {
                                // Monday first.
                                model: [1, 2, 3, 4, 5, 6, 0].map(d => card.modelData.days.find(x => x.day === d))
                                Chip {
                                    required property var modelData
                                    text: modelData.enabled ? modelData.name + " " + modelData.start + "–" + modelData.end
                                                            : modelData.name + " " + i18n("off")
                                    tint: modelData.enabled ? card.modelData.color : Kirigami.Theme.disabledTextColor
                                    opacity: modelData.enabled ? 1 : 0.6
                                }
                            }
                        }
                    }
                    Controls.ToolButton {
                        flat: true
                        icon.name: "document-edit"
                        text: i18n("Edit")
                        display: Controls.AbstractButton.IconOnly
                        onClicked: categoryDialog.edit(card.modelData)
                    }
                    Controls.ToolButton {
                        flat: true
                        visible: !card.modelData.isDefault
                        icon.name: "edit-delete"
                        text: i18n("Delete")
                        display: Controls.AbstractButton.IconOnly
                        onClicked: AppController.deleteCategory(card.modelData.id)
                        Controls.ToolTip.text: i18n("Delete; its tasks move to the default category")
                        Controls.ToolTip.visible: hovered
                        Controls.ToolTip.delay: Kirigami.Units.toolTipDelay
                    }
                }
            }
        }
    }
}
