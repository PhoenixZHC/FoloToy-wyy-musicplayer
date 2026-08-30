const path = require('node:path')

const apiRoot = path.join(__dirname, 'node_modules', 'NeteaseCloudMusicApi')
const { serveNcmApi } = require(path.join(apiRoot, 'server.js'))

const routes = [
  ['login_qr_key.js', '/login/qr/key'],
  ['login_qr_create.js', '/login/qr/create'],
  ['login_qr_check.js', '/login/qr/check'],
  ['login_status.js', '/login/status'],
  ['user_playlist.js', '/user/playlist'],
  ['playlist_track_all.js', '/playlist/track/all'],
  ['song_url.js', '/song/url'],
]

const moduleDefs = routes.map(([fileName, route]) => ({
  identifier: fileName.replace(/\.js$/i, ''),
  route,
  module: require(path.join(apiRoot, 'module', fileName)),
}))

serveNcmApi({
  port: Number(process.env.PORT || '3000'),
  host: '127.0.0.1',
  checkVersion: false,
  moduleDefs,
}).catch((error) => {
  console.error(error)
  process.exitCode = 1
})
