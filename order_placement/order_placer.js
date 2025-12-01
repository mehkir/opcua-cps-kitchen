const DISCOVERY_URL = "opc.tcp://localhost:4840";
const my_module = require('../cps-kitchen-dashboard/my-addons/my_module.node');
const { Kitchen } = require('../cps-kitchen-dashboard/browsenames');
const {
    ApplicationType,
    NodeId,
    resolveNodeId,
    OPCUAClient,
    AttributeIds,
    TimestampsToReturn,
    ClientSubscription,
    MonitoringParametersOptions,
    ReadValueIdOptions,
    extractFields
} = require("node-opcua");
const opcua_browser = require('../cps-kitchen-dashboard/opcua-browser.js');
const MersenneTwister = require('mersenne-twister');
const fs = require('fs');
const path = require('path');
const program = require("commander");
const { get } = require('http');

class subscriber {
    #endpoint_url;
    #client;
    #session;
    #subscription;

    constructor(_endpoint_url) {
        this.#endpoint_url = _endpoint_url;
    }

    async create_session() {
        try {
            // Create the client
            this.#client = OPCUAClient.create({ endpointMustExist: false });
            // Connect to OPC UA server
            await this.#client.connect(this.#endpoint_url);
            console.log("✅ Connected to OPC UA server");
            // Create session
            this.#session = await this.#client.createSession();
            console.log("✅ Session created");
        } catch (err) {
            console.error("❌ Error:", err);
        }
    }

    async create_subscription() {
        try {
            // Create a subscription
            this.#subscription = ClientSubscription.create(this.#session, {
                requestedPublishingInterval: 0,
                requestedLifetimeCount: 100,
                requestedMaxKeepAliveCount: 10,
                maxNotificationsPerPublish: 10,
                publishingEnabled: true,
                priority: 10
            });

            this.#subscription.on("started", () =>
                console.log("📡 Subscription started (ID:", this.#subscription.subscriptionId, ")")
            );
            this.#subscription.on("terminated", () => console.log("❌ Subscription terminated"));
        } catch (err) {
            console.error("❌ Error:", err);
        }
    }

    async subscribe(node_id, value_dto) {
        try {
            const monitoredItem = await this.#subscription.monitor(
                {
                    nodeId: node_id,
                    attributeId: AttributeIds.Value
                },
                {
                    samplingInterval: 0,
                    discardOldest: true,
                    queueSize: 1
                },
                TimestampsToReturn.Both
            );

            // Handle data change
            monitoredItem.on("changed", async (data_value) => {
                let value = data_value.value.value;
                // Ensure containers exist before any assignment
                const type_key = value_dto.type;
                const attribute_key = value_dto.attribute_name;
                if (type_key === Kitchen.TYPE) {
                    if (attribute_key === Kitchen.ASSIGNED_ORDERS) {
                        assigned_orders_callback(value);
                    } else if (attribute_key === Kitchen.DROPPED_ORDERS) {
                        dropped_orders_callback(value);
                    }
                }
            });
        } catch (err) {
            console.error("❌ Error:", err);
        }
    }

    async disconnect() {
        try {
            if (this.#subscription) {
                await this.#subscription.terminate();
            }
            if (this.#session) {
                await this.#session.close();
            }
            if (this.#client) {
                await this.#client.disconnect();
            }
            console.log("✅ Disconnected from server");
        } catch (err) {
            console.error(`❌ Error during disconnect: ${err}`);
        }
    }
}

async function browse_kitchen_instance (_server, _instance_id) {
    const kitchen = { 
        methods: {},
        attributes: {}
    };
    kitchen.instance_id = _instance_id;
    const opcua_browser_instance = new opcua_browser();
    /* Browse kitchen methods */
    const browse_methods_result = await opcua_browser_instance.browse_methods(_server.discoveryUrl, _instance_id);
    for (const method of browse_methods_result.references) {
        console.log(`Kitchen method: ${method.browseName.name} (${method.nodeId.toString()})`);
        kitchen.methods[method.browseName.name] = method.nodeId;
    }
    /* Browse kitchen attributes */
    const browse_attributes_result = await opcua_browser_instance.browse_attributes(_server.discoveryUrl, _instance_id);
    for (const attr of browse_attributes_result.references) {
        console.log(`Kitchen attribute: ${attr.browseName.name} (${attr.nodeId.toString()})`);
        if (attr.browseName.name === Kitchen.ASSIGNED_ORDERS || attr.browseName.name === Kitchen.DROPPED_ORDERS) {
            kitchen.attributes[attr.browseName.name] = attr.nodeId;
        }
    }
    kitchen.url = _server.discoveryUrl;
    return kitchen;
}

async function subscribe_kitchen (_kitchen) {
    kitchen_subscriber = new subscriber(_kitchen.url);
    await kitchen_subscriber.create_session();
    await kitchen_subscriber.create_subscription();
    for (const [browse_name, attribute_id] of Object.entries(_kitchen.attributes)) {
        const kitchen_monitor = {
            type: Kitchen.TYPE,
            attribute_name: browse_name
        };
        console.log(`Subscribing to kitchen attribute ${browse_name}`);
        await kitchen_subscriber.subscribe(attribute_id, kitchen_monitor);
    }
}

async function place_order() {
    if (order_queue.length === 0) {
        console.log("All orders have been placed.");
        return;
    }
    const order_id = order_queue[0];
}

function assigned_orders_callback(value) {
    console.log(`🍽️ Assigned Orders updated: ${value}`);
    if (value === 0) {
        place_order();
    } else {
        order_queue.shift();
        place_order();
    }
}

function dropped_orders_callback(value) {
    console.log(`❌ Dropped Orders updated: ${value}`);
    if (value === 0)
        return;
    place_order();
}

function random_int_in_range(min, max) {
    return Math.floor(mt_generator.random() * (max - min + 1)) + min;
}

function get_recipe_count() {
    const recipes_path = path.join(__dirname, '..', 'recipes.json');
    let count = 0;
    fs.readFile(recipes_path, 'utf8', (err, data) => {
        if (err) {
            console.error('Error reading recipes.json:', err);
            return;
        }

        try {
            const recipes = JSON.parse(data);
            count = Object.keys(recipes).length;
            console.log('Number of recipes:', count);
        } catch (parseErr) {
            console.error('Error parsing JSON:', parseErr);
        }
    });
    return count;
}

async function connect_to_kitchen() {
    console.log('Connecting to kitchen');
    kitchen.client = OPCUAClient.create({});
    kitchen.session = null;
    try {
        await kitchen.client.connect(kitchen.url);
        console.log("Connected to kitchen at", kitchen.url);
        kitchen.session = await kitchen.client.createSession();
        console.log("Session created with kitchen");
    } catch (err) {
        console.error("Error connecting to kitchen:", err);
        process.exit(1);
    }
}

async function place_order (_order_id) {
    await new Promise(resolve => setTimeout(resolve, 50));
    console.log("Place order");
    const method_id = kitchen.methods['PlaceOrder'];
    try {
        const result = await kitchen.session.call({
            objectId: kitchen.instance_id,
            methodId: method_id,
            inputArguments: [
            { dataType: "Int32", value: _order_id }
            ]
        });
        console.log("Method call result:", result);
    } catch (err) {
        console.error("Error calling kitchen method:", err);
    } finally {
        process.exit(1);
    }
}

let kitchen = null;
let kitchen_subscriber = null;
const order_queue = [];
const mt_generator = new MersenneTwister(5489);

async function main() {
    program.option("-oc, --order-count <number>", "Number of orders to place");
    program.parse(process.argv);
    const options = program.opts();
    const order_count = Number(options.orderCount);
    if (isNaN(order_count) || order_count <= 0) {
        console.log("A positive number is required for order Count");
        process.exit(1);
    }
    console.log("Order Count:", order_count);
    for (let i = 0; i < order_count; i++) {
        const order_id = mt_generator.random_int_in_range(1, get_recipe_count());
        order_queue.push(order_id);
    }
    const opcua_browser_instance = new opcua_browser();
    let servers;
    try {
        servers = my_module.findServers(DISCOVERY_URL);   
    } catch (error) {
        console.log(`${error} (is the discovery server started?)`);
        process.exit(1);
    }

    for (const server of servers) {
        if (server.applicationType !== ApplicationType.Server) {
            console.log(`Skipping non-server application: ${server.applicationUri}`);
            continue;
        }
        let instance_id;
        if (
            kitchen_subscriber === null &&
            (instance_id = await opcua_browser_instance.browse_instance(
                server.discoveryUrl,
                Kitchen.TYPE
            )) !== NodeId.nullNodeId
        ) {
            console.log(`Kitchen type found on server: ${server.discoveryUrl}`);
            kitchen = await browse_kitchen_instance(server, instance_id);
            await subscribe_kitchen(kitchen);
        }
    }

    // Cleanup on Ctrl+C
    process.on('SIGINT', () => {
        console.log('🛑 Shutting down...');
        (async () => {
            try {
                if (kitchen_subscriber) {
                    await kitchen_subscriber.disconnect();
                }
                if (kitchen.session){
                    await kitchen.session.close();
                }
                await kitchen.client.disconnect();
            } catch (err) {
                console.error("Error during kitchen disconnect:", err);
            } finally {
                process.exit(0);
            }
        })();
    });
}

main().catch(err => {
    console.error("Unhandled error in main:", err);
    process.exit(1);
});