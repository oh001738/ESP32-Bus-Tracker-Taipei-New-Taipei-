// ═══════════════════════════════════════════════════════
//  公車到站中繼站 - 最終精確方向版
// ═══════════════════════════════════════════════════════

function fetchGz(url) {
  var resp = UrlFetchApp.fetch(url, { muteHttpExceptions: true });
  return JSON.parse(Utilities.ungzip(resp.getBlob()).getDataAsString("UTF-8"));
}

function toArr(data) {
  return Array.isArray(data) ? data : (data.BusInfo || data.Result || []);
}

function doGet(e) {
  try {
    var cache = CacheService.getScriptCache();
    var cached = cache.get("busResult");
    if (cached) return ContentService.createTextOutput(cached).setMimeType(ContentService.MimeType.JSON);

    // ── 目標鎖定：棕19(往昆陽)、藍51(往展覽館)、629(往松山) ──────
    var targets = [
      { name: "棕19", city: "blobbus", rid: 16153, sid: 58077 }, // 改為 58077 (往昆陽)
      { name: "藍51", city: "blobbus", rid: 16506, sid: 130073 }, // 130073 (往展覽館)
      { name: "629",  city: "ntpcbus", rid: 5415,  sid: 14001  }  // 14001 (往松山)
    ];

    var result = {};
    targets.forEach(function(t) { result[t.name] = { sec: -1, status: 0 }; });

    var cities = ["blobbus", "ntpcbus"];
    cities.forEach(function(city) {
      var url = "https://tcgbusfs.blob.core.windows.net/" + city + "/GetEstimateTime.gz";
      var etas = toArr(fetchGz(url));
      
      etas.forEach(function(item) {
        targets.forEach(function(t) {
          if (t.city === city && item.RouteID == t.rid && item.StopID == t.sid) {
            result[t.name].sec = (item.EstimateTime == null) ? -1 : parseInt(item.EstimateTime);
            result[t.name].status = parseInt(item.StopStatus || 0);
          }
        });
      });
    });

    result.updated = Utilities.formatDate(new Date(), "Asia/Taipei", "HH:mm:ss");
    var json = JSON.stringify(result);
    cache.put("busResult", json, 25); 

    return ContentService.createTextOutput(json).setMimeType(ContentService.MimeType.JSON);

  } catch (err) {
    return ContentService.createTextOutput(JSON.stringify({ error: err.toString() })).setMimeType(ContentService.MimeType.JSON);
  }
}
