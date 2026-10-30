(function () {
  'use strict';

  var pw = document.getElementById('pw');
  var btn = document.getElementById('go');
  var err = document.getElementById('err');

  function fail(msg) {
    err.textContent = msg || '密码错误或登录暂不可用';
    btn.disabled = false;
  }

  function submit() {
    var pass = pw.value;
    if (!pass) return;
    err.textContent = '';
    btn.disabled = true;

    fetch('/api/auth', { credentials: 'same-origin', headers: { 'Accept': 'application/json' } })
      .then(function (r) { return r.json(); })
      .then(function (a) {
        if (!a || !a.salt || !a.nonce) {
          fail('登录不可用');
          return;
        }
        var proof = sha256Hex(a.nonce + kdfHash(a.salt, pass));
        return fetch('/api/login', {
          method: 'POST',
          credentials: 'same-origin',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ nonce: a.nonce, proof: proof })
        }).then(function (r) {
          if (r.status === 200) {
            location.replace('/');
          } else {
            fail();
          }
        });
      })
      .catch(function () {
        fail('网络错误');
      });
  }

  document.getElementById('lf').addEventListener('submit', function (e) {
    e.preventDefault();
    submit();
  });
})();
