// Copyright (c) 2017-2023 Cloudflare, Inc.
// Licensed under the Apache 2.0 license found in the LICENSE file or at:
//     https://opensource.org/licenses/Apache-2.0

let tick = 0;

addEventListener('fetch', event => {
  event.respondWith(handleRequest(event.request));
});

let url = 'https://www.baidu.com';

export default {
  async fetch(request, env) {
    //return await handleErrors(request, async () => {
    //  return await handleRequest(request);
    //});
    return await handleRequest(request);
  }
}

async function handleErrors(request, func) {
  try {
    return await func();
  } catch (err) {
    if (request.headers.get("Upgrade") == "websocket") {
      // Annoyingly, if we return an HTTP error in response to a WebSocket request, Chrome devtools
      // won't show us the response body! So... let's send a WebSocket response with an error
      // frame instead.
      let pair = new WebSocketPair();
      pair[1].accept();
      pair[1].send(JSON.stringify({error: err.stack}));
      pair[1].close(1011, "Uncaught exception during session setup");
      return new Response(null, { status: 101, webSocket: pair[0] });
    } else {
      return new Response(err.stack, {status: 500});
    }
  }
}

async function handle(request) {
  let cur = tick++;
  //return new Response("Hello World " + cur + "\n");
  
  let resp = await fetch(url);
  let baidu = await resp.text();
 
  return await new Promise(resolve => {
    const id = setInterval(() => {
      //if (condition) {
        resolve(new Response("Hello World " + cur + baidu + "\n"));
        clearInterval(id);
      //};
    }, 2000);
  });
}

async function handleRequest(request) {
  const upgradeHeader = request.headers.get('Upgrade');
  if (!upgradeHeader || upgradeHeader !== 'websocket') {
    return new Response('Expected Upgrade: websocket', { status: 426 });
  }

  const webSocketPair = new WebSocketPair();
  const [client, server] = Object.values(webSocketPair);

  server.accept();
  server.addEventListener('message', event => {
    console.log('worker.js: got ' + event.data);
    server.send(event.data);
    //server.close(1011, "WebSocket broken.");
  });

  let closeHandler = evt => {
    //server.close(1011, "WebSocket broken.");
    //client.close();
    console.log('closeHandler');
  };

  let errorHandler  = evt => {
    //server.close(1011, "WebSocket broken.");
    console.log('errorHandler');
  };

  server.addEventListener("close", closeHandler);
  server.addEventListener("error", errorHandler);

  return new Response(null, {
    status: 101,
    webSocket: client,
  });
}

