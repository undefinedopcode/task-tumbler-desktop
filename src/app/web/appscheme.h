#pragma once

#include <QWebEngineUrlSchemeHandler>

namespace tumbler {

// tumbler://app/… serves the built web UI from the resources (:/webui). The
// build is a single index.html, so every path that isn't a file gets it.
class AppScheme : public QWebEngineUrlSchemeHandler
{
    Q_OBJECT
public:
    using QWebEngineUrlSchemeHandler::QWebEngineUrlSchemeHandler;

    static constexpr const char *name = "tumbler";
    // Before QApplication: the scheme must be known when the engine starts.
    static void registerScheme();

    void requestStarted(QWebEngineUrlRequestJob *job) override;
};

} // namespace tumbler
