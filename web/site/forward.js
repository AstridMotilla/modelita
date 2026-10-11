// Shared programs used to live at the site's root (modelita.vercel.app/#z...).
// The playground is at /play/ now, so send those links on, program and all.
if (/^#[zt][A-Za-z0-9_-]+$/.test(location.hash)) location.replace("/play/" + location.hash);
