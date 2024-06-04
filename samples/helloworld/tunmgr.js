// Copyright (c) 2017-2023 Cloudflare, Inc.
// Licensed under the Apache 2.0 license found in the LICENSE file or at:
//     https://opensource.org/licenses/Apache-2.0

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

// In modules-syntax workers, we use `export default` to export our script's main event handlers.
// Here, we export one handler, `fetch`, for receiving HTTP requests. In pre-modules workers, the
// fetch handler was registered using `addEventHandler("fetch", event => { ... })`; this is just
// new syntax for essentially the same thing.
//
// `fetch` isn't the only handler. If your worker runs on a Cron schedule, it will receive calls
// to a handler named `scheduled`, which should be exported here in a similar way. We will be
// adding other handlers for other types of events over time.
export default {
  async fetch(request, env) {
    return await handleErrors(request, async () => {
      // We have received an HTTP request! Parse the URL and route the request.

      let url = new URL(request.url);
      let path = url.pathname.slice(1).split('/');

      if (!path[0]) {
        // Serve our HTML at the root path.
        return new Response("Not found", {status: 404});
      }

      switch (path[0]) {
        case "tun":
          // This is a request for `/api/...`, call the API handler.
          return handleTunRequest(request, env);

        default:
          return new Response("Not found", {status: 404});
      }
    });
  }
}

async function handleTunRequest(request, env) {
  // Get the Durable Object stub for this 'tunmgr' instance! The stub is a client object that can be used
  // to send messages to the remote Durable Object instance. The stub is returned immediately;
  // there is no need to await it. This is important because you would not want to wait for
  // a network round trip before you could start sending requests. Since Durable Objects are
  // created on-demand when the ID is first used, there's nothing to wait for anyway; we know
  // an object will be available somewhere to receive our requests.
  let name = 'tunmgr';
  let id = env.tunmgrs.idFromName(name);
  let tunmgr = env.tunmgrs.get(id);

  // Send the request to the object. The `fetch()` method of a Durable Object stub has the
  // same signature as the global `fetch()` function, but the request is always sent to the
  // object, regardless of the request's URL.
  return tunmgr.fetch(request);
}

// =======================================================================================
// The TunMgr Durable Object Class
export class TunMgr {
  constructor(controller, env) {
    // `controller.storage` provides access to our durable storage. It provides a simple KV
    // get()/put() interface.
    this.storage = controller.storage;

    // `env` is our environment bindings (discussed earlier).
    this.env = env;

    // We will put the WebSocket objects for each client, along with some metadata, into
    // `sessions`.
    this.tunnels = [];

    // tunnels index
    this.index = 0;
  }

  // The system will call fetch() whenever an HTTP request is sent to this Object. Such requests
  // can only be sent from other Worker code, such as the code above; these requests don't come
  // directly from the internet. In the future, we will support other formats than HTTP for these
  // communications, but we started with HTTP for its familiarity.
  async fetch(request) {
    let mgr = this;
    return await handleErrors(request, async () => {
	  const upgradeHeader = request.headers.get('Upgrade');
	  if (!upgradeHeader || upgradeHeader !== 'websocket') {
	    return new Response('Expected Upgrade: websocket', { status: 426 });
	  }

	  const webSocketPair = new WebSocketPair();
	  const [client, server] = Object.values(webSocketPair);
	  let index = mgr.index;
	  mgr.index++;

	  server.accept();
	  server.addEventListener('message', event => {
	    console.log('tunmgr: got from ' + index + ': '+ event.data);
	    server.send(event.data);
	  });

	  let closeHandler = evt => {
	    console.log('closeHandler');
	  };

	  let errorHandler  = evt => {
	    console.log('errorHandler');
	  };

	  server.addEventListener("close", closeHandler);
	  server.addEventListener("error", errorHandler);

	  return new Response(null, {
	    status: 101,
	    webSocket: client,
	  });
    });
  }
}

