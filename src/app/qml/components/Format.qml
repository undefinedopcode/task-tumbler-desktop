pragma Singleton

import QtQuick

// Formatting shared by every page. Instants arrive as epoch milliseconds and
// display in the system zone, matching the C++ side. Singletons can't see the
// i18n() context functions, hence qsTr here.
QtObject {
    readonly property var priorityColors: ({
        urgent: "#e5484d",
        high: "#f59e0b",
        medium: "#6366f1",
        low: "#8b8d98"
    })

    // Shortcut hints the Mac way: "Ctrl+N" reads "⌘N" there (Qt already maps
    // the shortcuts themselves), and the global capture key is ⇧⌘Space.
    function keys(text) {
        if (Qt.platform.os !== "osx")
            return text
        return text.replace(/Meta\+Shift\+Space/g, "⇧⌘Space")
                   .replace(/Ctrl\+Shift\+/g, "⇧⌘")
                   .replace(/Ctrl\+Enter/g, "⌘↩")
                   .replace(/Ctrl\+/g, "⌘")
    }

    function time(ms) {
        return Qt.formatTime(new Date(ms), Qt.locale().timeFormat(Locale.ShortFormat))
    }

    function timeRange(start, end) {
        return time(start) + " – " + time(end)
    }

    function minutes(total) {
        if (total < 60)
            return qsTr("%1 min").arg(total)
        const h = Math.floor(total / 60)
        const m = total % 60
        return m === 0 ? qsTr("%1 h").arg(h) : qsTr("%1 h %2 min").arg(h).arg(m)
    }

    // 1:05:09 or 5:09
    function elapsed(ms) {
        const s = Math.max(0, Math.floor(ms / 1000))
        const h = Math.floor(s / 3600)
        const m = Math.floor((s % 3600) / 60)
        const sec = s % 60
        const pad = n => (n < 10 ? "0" : "") + n
        return h > 0 ? h + ":" + pad(m) + ":" + pad(sec) : m + ":" + pad(sec)
    }

    function parseDate(iso) {
        if (!iso)
            return new Date(NaN)
        const p = iso.split("-")
        return new Date(parseInt(p[0]), parseInt(p[1]) - 1, parseInt(p[2]))
    }

    function isoDate(d) {
        return isNaN(d.valueOf()) ? "" : Qt.formatDate(d, "yyyy-MM-dd")
    }

    // "Today", "Tomorrow", "Fri 30 Oct", relative to the local date.
    function dueLabel(iso) {
        if (!iso)
            return ""
        const d = parseDate(iso)
        const today = new Date()
        today.setHours(0, 0, 0, 0)
        const days = Math.round((d - today) / 86400000)
        if (days === 0)
            return qsTr("Today")
        if (days === 1)
            return qsTr("Tomorrow")
        if (days === -1)
            return qsTr("Yesterday")
        return Qt.formatDate(d, "ddd d MMM")
    }

    function priorityLabel(p) {
        return { urgent: qsTr("Urgent"), high: qsTr("High"), medium: qsTr("Medium"), low: qsTr("Low") }[p] || p
    }

    function energyLabel(e) {
        return { low: qsTr("Low energy"), medium: qsTr("Medium energy"), high: qsTr("High energy"), peak: qsTr("Peak energy") }[e] || e
    }

    function statusLabel(s) {
        return { pending: qsTr("To do"), in_progress: qsTr("In progress"), completed: qsTr("Done"), cancelled: qsTr("Cancelled") }[s] || s
    }
}
