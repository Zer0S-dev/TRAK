/* TRAK 3.0.5 — UI module */

function showView(name, button) {
    document.querySelectorAll('.view').forEach(view => view.classList.remove('active'));
    if (name !== 'home') $('view-' + name)?.classList.add('active');
    document.querySelectorAll('.nav-btn').forEach(btn => btn.classList.remove('active'));
    button?.classList.add('active');
    document.body.classList.remove('full-map');
    $('fullMapBtn')?.classList.remove('active');
    if (name === 'home') window.setTimeout(() => trackerMap?.invalidateSize(), 80);
}

function prototypeNotice() {
    alert('Cette fonction n’est pas active dans le firmware TRAK 3.0 prototype.');
}
function advanceSentinel() { prototypeNotice(); }
function saveSentinelTrakPhone() { prototypeNotice(); }
function deleteSentinelTrakPhone() { prototypeNotice(); }
function saveDataPlan() { prototypeNotice(); }
