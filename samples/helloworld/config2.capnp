# Imports the base schema for workerd configuration files.

# Refer to the comments in /src/workerd/server/workerd.capnp for more details.

using Workerd = import "/workerd/workerd.capnp";
# A constant of type `Workerd.Config` will be recognized as the top-level configuration.
const config :Workerd.Config = (
  # We have one nanoservice: the chat worker.
  services = [ (name = "tunmgr", worker = .tunMgrWorker) ],

  # We export it via HTTP on port 8080.
  sockets = [ ( name = "http", address = "*:8080", http = (), service = "tunmgr" ) ],
);

# For legibility we define the Worker's config as a separate constant.
const tunMgrWorker :Workerd.Worker = (
  # All Workers must declare a compatibility date, which ensures that if `workerd` is updated to
  # a newer version with breaking changes, it will emulate the API as it existed on this date, so
  # the Worker won't break.
  compatibilityDate = "2023-02-28",

  # This worker is modules-based.
  modules = [
    # Our code is in an ES module (JavaScript).
    (name = "tunmgr.js", esModule = embed "tunmgr.js"),
  ],

  # The Worker has two Durable Object classes, each of which needs an attached namespace.
  # The `uniqueKey`s can be any string, and are used to generate IDs. Keep the keys secret if you
  # don't want clients to be able to forge valid IDs -- or don't, if you don't care about that.
  #
  # In the example here, we've generated 32-character random hex keys, but again, the string can
  # be anything. These were generated specifically for this demo config; we do not use these
  # values in production.
  durableObjectNamespaces = [
    (className = "TunMgr", uniqueKey = "210bd0cbd803ef7883a1ee9d86cce06e", preventEviction = true),
  ],

  # To use Durable Objects we must declare how they are stored.
  #
  # As of this writing, `workerd` supports in-memory-only Durable Objects -- so, not really
  # "durable", as all data is lost when workerd restarts. However, this still allows us to run the
  # chat demo for testing purposes. (We plan to add actual storage for Durable Objects eventually,
  # but the storage system behind Cloudflare Workers is inherently tied to our network so did not
  # make sense to release as-is.)
  durableObjectStorage = (inMemory = void),

  # We must declare bindings to allow us to call back to our own Durable Object namespaces. These
  # show up as properties on the `env` object passed to `fetch()`.
  bindings = [
    (name = "tunmgrs", durableObjectNamespace = "TunMgr"),
  ],
);

