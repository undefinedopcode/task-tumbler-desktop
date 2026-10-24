/**
 * Runs the web app's occurrence generator (copied verbatim from
 * task-tumbler/server/src/routes/tasks.ts) over a matrix of patterns and
 * ranges, printing raw results as JSON. tests/fixtures/legacy_recurrence.json
 * was built from this output plus the intended result for each case, with a
 * reason recorded wherever the two differ.
 *
 *   TZ=America/Vancouver ~/projects/task-tumbler/server/node_modules/.bin/tsx tools/legacy-recurrence.ts
 *
 * The date-fns import below points into the web repo's node_modules.
 */
import { parseISO, addDays } from "/home/april/projects/task-tumbler/server/node_modules/date-fns/index.js";
interface RecurrencePattern { type: "daily"|"weekly"; interval: number; days?: number[]; endDate?: string; startDate?: string; }
function formatUTCDate(date: Date): string {
  return `${date.getUTCFullYear()}-${String(date.getUTCMonth() + 1).padStart(2, "0")}-${String(date.getUTCDate()).padStart(2, "0")}`;
}

// Helper function to generate occurrence dates
function generateOccurrenceDates(
  pattern: RecurrencePattern,
  rangeStart: Date,
  rangeEnd: Date,
): Date[] {
  const dates: Date[] = [];

  // Use pattern.startDate as anchor, or rangeStart if not set
  const anchorDate = pattern.startDate
    ? parseISO(pattern.startDate)
    : rangeStart;

  // Create UTC midnight dates to avoid timezone shifts
  const anchor = new Date(
    Date.UTC(
      anchorDate.getFullYear(),
      anchorDate.getMonth(),
      anchorDate.getDate(),
    ),
  );

  const start = new Date(
    Date.UTC(
      rangeStart.getFullYear(),
      rangeStart.getMonth(),
      rangeStart.getDate(),
    ),
  );

  const end = new Date(
    Date.UTC(rangeEnd.getFullYear(), rangeEnd.getMonth(), rangeEnd.getDate()),
  );

  const patternEnd = pattern.endDate
    ? new Date(
        parseISO(pattern.endDate).toISOString().split("T")[0] +
          "T00:00:00.000Z",
      )
    : null;

  const DAY_MS = 24 * 60 * 60 * 1000;

  if (pattern.type === "daily") {
    // For daily patterns with interval > 1, calculate which dates align with anchor
    const intervalMs = pattern.interval * DAY_MS;

    // Find the first occurrence on or after rangeStart that aligns with anchor
    const daysSinceAnchor = Math.floor(
      (start.getTime() - anchor.getTime()) / DAY_MS,
    );
    const daysUntilNext =
      (pattern.interval - (daysSinceAnchor % pattern.interval)) %
      pattern.interval;
    let current = new Date(start.getTime() + daysUntilNext * DAY_MS);

    while (current <= end) {
      if (patternEnd && current > patternEnd) break;
      if (current >= start) {
        dates.push(new Date(current));
      }
      current = new Date(current.getTime() + intervalMs);
    }
  } else if (pattern.type === "weekly") {
    // For weekly patterns, we need to track which week we're in relative to anchor
    const weekMs = 7 * DAY_MS;

    // Calculate weeks since anchor
    const msSinceAnchor = start.getTime() - anchor.getTime();
    const weeksSinceAnchor = Math.floor(msSinceAnchor / weekMs);

    // Find the start of the current interval-aligned week
    const weeksIntoInterval = weeksSinceAnchor % pattern.interval;
    const alignedWeekStart = new Date(
      start.getTime() - weeksIntoInterval * weekMs,
    );

    // Adjust to start of that week (Sunday)
    const dayOfAlignedWeek = alignedWeekStart.getUTCDay();
    let weekStart = new Date(
      alignedWeekStart.getTime() - dayOfAlignedWeek * DAY_MS,
    );

    // If we've gone before the anchor, move forward by interval
    if (weekStart < anchor) {
      const weeksToAdd = Math.ceil(
        (anchor.getTime() - weekStart.getTime()) / weekMs,
      );
      weekStart = new Date(weekStart.getTime() + weeksToAdd * weekMs);
    }

    let current = weekStart;
    while (current <= end) {
      // Check each day of this week
      for (let d = 0; d < 7; d++) {
        const checkDate = new Date(current.getTime() + d * DAY_MS);
        if (checkDate < start) continue;
        if (checkDate > end) break;
        if (patternEnd && checkDate > patternEnd) break;

        const dayOfWeek = checkDate.getUTCDay();
        if (pattern.days && pattern.days.includes(dayOfWeek)) {
          dates.push(new Date(checkDate));
        }
      }

      // Move to next interval-aligned week
      current = new Date(current.getTime() + pattern.interval * weekMs);
    }
  }

  return dates;
}


const cases: any[] = [];
const anchors = ["2026-03-04","2026-03-08","2026-03-01","2026-02-26"]; // Wed, Sun, Sun, Thu
const ranges = [["2026-03-04","2026-03-31"],["2026-03-08","2026-04-20"],["2026-03-10","2026-03-12"],["2026-02-20","2026-03-20"]];
const patterns: RecurrencePattern[] = [
  {type:"daily",interval:1},{type:"daily",interval:2},{type:"daily",interval:3},
  {type:"weekly",interval:1,days:[1,3,5]},{type:"weekly",interval:1,days:[0,6]},
  {type:"weekly",interval:2,days:[1,4]},{type:"weekly",interval:3,days:[2]},
];
for (const p of patterns) for (const a of anchors) for (const [s,e] of ranges) for (const end of [undefined,"2026-03-15"]) {
  const pat = {...p, startDate: a, ...(end?{endDate:end}:{})};
  const out = generateOccurrenceDates(pat, parseISO(s), parseISO(e)).map(formatUTCDate);
  cases.push({pattern: pat, from: s, to: e, legacy: out});
}
console.log(JSON.stringify(cases));
