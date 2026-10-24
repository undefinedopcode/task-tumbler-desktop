#pragma once

#include <QString>
#include <QStringList>
#include <optional>

namespace tumbler {

// What an event allows to be scheduled during it. The web app stored and
// edited this but never enforced it; the scheduler enforces it here.
enum class BlockingMode {
    None,          // allow all tasks
    BlockAll,      // no tasks during the event
    RecurringOnly, // only recurring tasks
    TagsOnly,      // only tasks carrying at least one allowed tag
};

QString nameOf(BlockingMode mode);
std::optional<BlockingMode> blockingModeFromString(const QString &s);

struct EventBlockingRule {
    BlockingMode mode = BlockingMode::None;
    QStringList allowedTags;

    // Tags compare case-insensitively after trimming, since they were typed
    // by hand in several places (task form, Trello labels, event form).
    bool allows(bool taskIsRecurring, const QStringList &taskTags) const;
};

} // namespace tumbler
