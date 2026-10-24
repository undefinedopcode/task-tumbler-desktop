#include "blocking.h"

namespace tumbler {

QString nameOf(BlockingMode mode)
{
    switch (mode) {
    case BlockingMode::None:
        return QStringLiteral("none");
    case BlockingMode::BlockAll:
        return QStringLiteral("block_all");
    case BlockingMode::RecurringOnly:
        return QStringLiteral("recurring_only");
    case BlockingMode::TagsOnly:
        return QStringLiteral("tags_only");
    }
    return {};
}

std::optional<BlockingMode> blockingModeFromString(const QString &s)
{
    if (s == QLatin1String("none"))
        return BlockingMode::None;
    if (s == QLatin1String("block_all"))
        return BlockingMode::BlockAll;
    if (s == QLatin1String("recurring_only"))
        return BlockingMode::RecurringOnly;
    if (s == QLatin1String("tags_only"))
        return BlockingMode::TagsOnly;
    return std::nullopt;
}

bool EventBlockingRule::allows(bool taskIsRecurring, const QStringList &taskTags) const
{
    switch (mode) {
    case BlockingMode::None:
        return true;
    case BlockingMode::BlockAll:
        return false;
    case BlockingMode::RecurringOnly:
        return taskIsRecurring;
    case BlockingMode::TagsOnly:
        for (const QString &tag : taskTags) {
            const QString t = tag.trimmed();
            for (const QString &allowed : allowedTags) {
                if (t.compare(allowed.trimmed(), Qt::CaseInsensitive) == 0)
                    return true;
            }
        }
        return false;
    }
    return false;
}

} // namespace tumbler
