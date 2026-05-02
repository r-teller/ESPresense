/*
 * Web UI Routes
 *
 * Compressed Size Summary:
 * ui_app_immutable_assets_css: 25,144 bytes
 * ui_html: 4,320 bytes
 * ui_app_immutable_entry_js: 67,041 bytes
 * ui_app_immutable_nodes_js: 590 bytes
 * ui_svg: 456 bytes
 * Total: 97,551 bytes
 */

#pragma once

#include <ESPAsyncWebServer.h>
#include "ui_app_immutable_assets_css.h"
#include "ui_html.h"
#include "ui_app_immutable_entry_js.h"
#include "ui_app_immutable_nodes_js.h"
#include "ui_svg.h"

inline void setupRoutes(AsyncWebServer* server) {
    server->on("/app/immutable/assets/internal.BieM-44V.css", HTTP_GET, serveAppImmutableAssetsInternalBieM_44VCss);
    server->on("/app/immutable/assets/start.BieM-44V.css", HTTP_GET, serveAppImmutableAssetsStartBieM_44VCss);
    server->on("/app/immutable/entry/app.CfwlWog8.js", HTTP_GET, serveAppImmutableEntryAppCfwlWog8Js);
    server->on("/app/immutable/entry/start.C0gEJdOd.js", HTTP_GET, serveAppImmutableEntryStartC0gEJdOdJs);
    server->on("/app/immutable/nodes/0.ATFeEbqw.js", HTTP_GET, serveAppImmutableNodes_0AtFeEbqwJs);
    server->on("/app/immutable/nodes/1.DLmHK0CN.js", HTTP_GET, serveAppImmutableNodes_1DLmHk0CnJs);
    server->on("/app/immutable/nodes/2.CFixMBaW.js", HTTP_GET, serveAppImmutableNodes_2CFixMBaWJs);
    server->on("/app/immutable/nodes/3.ARhb4uZG.js", HTTP_GET, serveAppImmutableNodes_3ARhb4uZgJs);
    server->on("/app/immutable/nodes/4.Bbaop4id.js", HTTP_GET, serveAppImmutableNodes_4Bbaop4idJs);
    server->on("/app/immutable/nodes/5.Y6STygvj.js", HTTP_GET, serveAppImmutableNodes_5Y6STygvjJs);
    server->on("/app/immutable/nodes/6.BxbpWcEN.js", HTTP_GET, serveAppImmutableNodes_6BxbpWcEnJs);
    server->on("/app/immutable/nodes/7.gStV2Ac2.js", HTTP_GET, serveAppImmutableNodes_7GStV2Ac2Js);
    server->on("/favicon.svg", HTTP_GET, serveFaviconSvg);
    // HTML routes
    server->on("/devices", HTTP_GET, serveDevicesHtml);
    server->on("/devices.html", HTTP_GET, serveDevicesHtml);
    server->on("/fingerprints", HTTP_GET, serveFingerprintsHtml);
    server->on("/fingerprints.html", HTTP_GET, serveFingerprintsHtml);
    server->on("/hardware", HTTP_GET, serveHardwareHtml);
    server->on("/hardware.html", HTTP_GET, serveHardwareHtml);
    server->on("/", HTTP_GET, serveIndexHtml);
    server->on("/network", HTTP_GET, serveNetworkHtml);
    server->on("/network.html", HTTP_GET, serveNetworkHtml);
    server->on("/settings", HTTP_GET, serveSettingsHtml);
    server->on("/settings.html", HTTP_GET, serveSettingsHtml);
}
