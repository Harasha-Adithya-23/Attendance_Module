/*
 * app.js  -  A600 Attendance System front-end
 * Talks to the HTTP server running on the STM32MP135F-DK board.
 */

var BASE = window.location.origin;

var logArea = document.getElementById("logarea");

function appendLog(text) {
    var ts = new Date().toLocaleTimeString();
    logArea.value += "[" + ts + "] " + text + "\n";
    logArea.scrollTop = logArea.scrollHeight;
}

function setButtonsBusy(busy) {
    document.querySelectorAll("button").forEach(function(b) {
        b.disabled = busy;
    });
}

function getUserId() {
    var raw = document.getElementById("userid").value.trim();
    var id  = parseInt(raw, 10);
    if (isNaN(id) || id < 0 || id > 63) return -1;
    return id;
}

/* POST to an endpoint, show the JSON result in the log */
function apiPost(endpoint, params) {
    var url = BASE + endpoint + (params ? "?" + params : "");
    setButtonsBusy(true);
    appendLog("-> " + endpoint + (params ? " " + params : ""));

    fetch(url, { method: "POST" })
        .then(function(res) { return res.json(); })
        .then(function(data) {
            if (data.ok) {
                if (data.action === "login") {
                    appendLog("LOGIN  - User " + data.user_id +
                              "  |  Session #" + data.session +
                              "  |  In: " + data.login +
                              "  |  Total so far: " + data.total_so_far.toFixed(2) + " h");
                } else if (data.action === "logout") {
                    appendLog("LOGOUT - User " + data.user_id +
                              "  |  Session #" + data.session +
                              "  |  In: " + data.login +
                              "  -> Out: " + data.logout +
                              "  |  Session: " + data.session_hours.toFixed(2) + " h" +
                              "  |  Total today: " + data.total_today.toFixed(2) + " h");
                } else if (data.sessions !== undefined) {
                    appendLog("END DAY - User " + data.user_id +
                              "  |  " + data.sessions + " session(s)" +
                              "  |  " + data.total_hours.toFixed(2) + " h" +
                              "  |  Status: " + data.status +
                              "  |  Date: " + data.date);
                } else {
                    appendLog("OK: " + (data.msg || JSON.stringify(data)));
                }
            } else {
                appendLog("ERROR: " + (data.msg || JSON.stringify(data)));
            }
        })
        .catch(function(err) {
            appendLog("Network error: " + err.message);
        })
        .finally(function() {
            setButtonsBusy(false);
        });
}

/* Action functions */

function identify() {
    appendLog("Place finger on sensor...");
    apiPost("/identify", "");
}

function enroll() {
    var id = getUserId();
    if (id < 0) { appendLog("Enter a valid User ID (0-63) before enrolling."); return; }
    appendLog("Place finger for User " + id + "...");
    apiPost("/enroll", "id=" + id);
}

function deleteUser() {
    var id = getUserId();
    if (id < 0) { appendLog("Enter a valid User ID (0-63) before deleting."); return; }
    if (!confirm("Delete User " + id + "? This cannot be undone.")) return;
    apiPost("/delete", "id=" + id);
}

function endDay() {
    var id = getUserId();
    if (id < 0) { appendLog("Enter a valid User ID (0-63) for End Day."); return; }
    apiPost("/endday", "id=" + id);
}

function resetCSV() {
    if (!confirm("Reset Biometric.csv? All records will be deleted.")) return;
    setButtonsBusy(true);
    fetch(BASE + "/reset", { method: "POST" })
        .then(function(res) { return res.json(); })
        .then(function(data) {
            appendLog(data.ok ? data.msg : "ERROR: " + data.msg);
        })
        .catch(function(err) {
            appendLog("Network error: " + err.message);
        })
        .finally(function() {
            setButtonsBusy(false);
        });
}

function clearLog() {
    logArea.value = "";
}

function refreshStatus() {
    fetch(BASE + "/status")
        .then(function(res) { return res.json(); })
        .then(function(data) {
            var statusEl = document.getElementById("status");
            if (!data.ok || !statusEl) return;
            if (data.enrolled.length === 0) {
                statusEl.textContent = "No users enrolled.";
                return;
            }
            var rows = data.enrolled.map(function(u) {
                return "User " + String(u.id).padStart(2, "0") +
                       "  " + (u.in ? "[IN ]" : "[OUT]") +
                       "  Sessions: " + u.sessions +
                       "  Total: " + u.total.toFixed(2) + " h";
            }).join("\n");
            statusEl.textContent = rows;
        })
        .catch(function() { /* silent */ });
}

/* Wire up buttons */
document.getElementById("plcbtn")   .addEventListener("click", identify);
document.getElementById("enrollbtn").addEventListener("click", enroll);
document.getElementById("removebtn").addEventListener("click", deleteUser);
document.getElementById("endday")   .addEventListener("click", endDay);
document.getElementById("reset")    .addEventListener("click", resetCSV);
document.getElementById("clear")    .addEventListener("click", clearLog);

/* Auto-refresh enrolled status every 10 s */
refreshStatus();
setInterval(refreshStatus, 10000);

appendLog("Attendance System connected to " + BASE);
