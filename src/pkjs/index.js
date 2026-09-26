// Phone settings page (gear icon): upload or clear the photo. Plain data: URL page, no library.
// Timer, background and screensaver settings are on the watch (hold Select when the time is reset).
var W = 144, H = 168, ROW_BYTES = W / 8, CHUNK_ROWS = 8;

// Runs inside the settings page (inserted via toString). Converts the photo to 1-bit, as hex.
function pageScript() {
  var $ = function(id) { return document.getElementById(id); };
  var src = null, photo = '';
  var KERNELS = {  // [dx, dy, weight], divisor
    atkinson: [[[1, 0, 1], [2, 0, 1], [-1, 1, 1], [0, 1, 1], [1, 1, 1], [0, 2, 1]], 8],
    floyd: [[[1, 0, 7], [-1, 1, 3], [0, 1, 5], [1, 1, 1]], 16],
    none: [[], 1]
  };

  function draw() {
    if (!src) return;
    var c = $('p'), g = c.getContext('2d');
    g.fillStyle = '#fff'; g.fillRect(0, 0, W, H);
    var fit = $('fill').checked ? Math.max : Math.min;
    var k = fit(W / src.width, H / src.height), w = src.width * k, h = src.height * k;
    g.drawImage(src, (W - w) / 2, (H - h) / 2, w, h);
    var id = g.getImageData(0, 0, W, H), d = id.data, lum = [], bytes = [], i, x, y;
    for (i = 0; i < W * H; i++) lum.push(d[i * 4] * 0.3 + d[i * 4 + 1] * 0.59 + d[i * 4 + 2] * 0.11);
    // Auto contrast (2%..98% to black..white), then the manual contrast and brightness.
    var sorted = lum.slice().sort(function(a, b) { return a - b; });
    var lo = sorted[Math.floor(sorted.length * 0.02)], hi = Math.max(sorted[Math.floor(sorted.length * 0.98)], lo + 1);
    var contrast = +$('contrast').value / 100, bright = +$('bright').value, invert = $('invert').checked;
    for (i = 0; i < lum.length; i++) {
      var v = ((lum[i] - lo) * 255 / (hi - lo) - 128) * contrast + 128 + bright;
      lum[i] = invert ? 255 - v : v;
    }
    for (i = 0; i < ROW_BYTES * H; i++) bytes.push(0);
    var kern = KERNELS[$('dither').value], spread = kern[0], div = kern[1];
    for (y = 0; y < H; y++) for (x = 0; x < W; x++) {
      var val = lum[y * W + x], white = val >= 128, err = (val - (white ? 255 : 0)) / div;
      for (var j = 0; j < spread.length; j++) {
        var nx = x + spread[j][0], ny = y + spread[j][1];
        if (nx >= 0 && nx < W && ny < H) lum[ny * W + nx] += err * spread[j][2];
      }
      if (white) bytes[y * ROW_BYTES + (x >> 3)] |= 1 << (x & 7);  // Pebble 1-bit: LSB = left, 1 = white
      var o = (y * W + x) * 4;
      d[o] = d[o + 1] = d[o + 2] = white ? 255 : 0;
    }
    g.putImageData(id, 0, 0);  // preview shows what the watch will show
    photo = bytes.map(function(b) { return (b < 16 ? '0' : '') + b.toString(16); }).join('');
  }

  $('f').onchange = function() {
    var img = new Image();
    img.onload = function() { src = img; $('tune').style.display = ''; draw(); };
    img.src = URL.createObjectURL(this.files[0]);
  };
  ['contrast', 'bright', 'fill', 'dither', 'invert'].forEach(function(id) { $(id).oninput = $(id).onchange = draw; });
  $('reset').onclick = function() {
    $('contrast').value = 100; $('bright').value = 0; $('fill').checked = false;
    $('dither').value = 'atkinson'; $('invert').checked = false; draw();
  };
  $('save').onclick = function() {
    location.href = 'pebblejs://close#' + (photo ? encodeURIComponent(JSON.stringify({ Photo: photo })) : '');
  };
  $('clear').onclick = function() {
    location.href = 'pebblejs://close#' + encodeURIComponent(JSON.stringify({ ClearPhoto: 1 }));
  };
}

function page() {
  return '<!doctype html><meta name="viewport" content="width=device-width">' +
    '<style>body{font:18px sans-serif;padding:16px}p{margin:10px 0}input[type=range]{width:100%}' +
    'td{padding:2px 8px 2px 0;vertical-align:top}</style>' +
    '<h2>Manual</h2><table>' +
    '<tr><td><b>Up</b></td><td>Home +1. Double-click: −1. Hold: 0.</td></tr>' +
    '<tr><td><b>Down</b></td><td>Away +1. Double-click: −1. Hold: 0.</td></tr>' +
    '<tr><td><b>Select</b></td><td>Start or pause the time. Stop the alarm.</td></tr>' +
    '<tr><td><b>Hold Select</b></td><td>Reset the time. Hold again: watch settings.</td></tr>' +
    '<tr><td><b>Hold Up + Down</b></td><td>Reset the time and the scores.</td></tr>' +
    '</table><p>At 0:00 the watch vibrates until you push Select.</p>' +
    '<h2>Image</h2>' +
    '<p>Without a photo there is no background image and no screensaver.</p>' +
    '<p>New photo: <input id="f" type="file" accept="image/*"></p>' +
    '<div id="tune" style="display:none">' +
    '<canvas id="p" width="144" height="168" style="border:1px solid #888;width:288px;image-rendering:pixelated"></canvas>' +
    '<p>Brightness<br><input id="bright" type="range" min="-128" max="128" value="0"></p>' +
    '<p>Contrast<br><input id="contrast" type="range" min="25" max="300" value="100"></p>' +
    '<p>Dither <select id="dither"><option value="atkinson">Atkinson (sharp)</option>' +
    '<option value="floyd">Floyd-Steinberg (smooth)</option><option value="none">None (black/white)</option></select></p>' +
    '<p><label><input id="fill" type="checkbox"> Fill screen (crop)</label></p>' +
    '<p><label><input id="invert" type="checkbox"> Invert</label></p>' +
    '<p><button id="reset">Reset controls</button></p>' +
    '</div>' +
    '<p><button id="save" style="font-size:18px">Save</button> <button id="clear" style="font-size:18px">Clear photo</button></p>' +
    '<script>var W = ' + W + ', H = ' + H + ', ROW_BYTES = ' + ROW_BYTES + ';(' + pageScript.toString() + ')()</script>';
}

function hexChunk(hex, row) {
  var bytes = [], start = row * ROW_BYTES * 2;
  for (var i = 0; i < CHUNK_ROWS * ROW_BYTES; i++) bytes.push(parseInt(hex.substr(start + i * 2, 2), 16));
  return bytes;
}

// AppMessages must go one at a time; retry a chunk a few times if the watch is busy.
function sendPhoto(hex, row, tries) {
  if (row >= H) return;
  Pebble.sendAppMessage({ PhotoRow: row, PhotoData: hexChunk(hex, row) },
    function() { sendPhoto(hex, row + CHUNK_ROWS, 0); },
    function() { if (tries < 5) setTimeout(function() { sendPhoto(hex, row, tries + 1); }, 500); });
}

Pebble.addEventListener('showConfiguration', function() {
  Pebble.openURL('data:text/html,' + encodeURIComponent(page()));
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e.response) return;  // closed without Save
  var c = JSON.parse(decodeURIComponent(e.response));
  if (c.ClearPhoto) Pebble.sendAppMessage({ ClearPhoto: 1 });
  else if (c.Photo && c.Photo.length == W * H / 4) sendPhoto(c.Photo, 0, 0);
});
