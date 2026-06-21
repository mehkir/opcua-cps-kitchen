const WebSocket = require('ws');
const {program} = require("commander");

const PLACE_RANDOM_ORDER = "PlaceRandomOrder";
const COMPLETED_ORDERS = "CompletedOrders";
const ALL_AGENTS_DISCOVERED_TYPE = "AllAgentsDiscovered";
let ws;
function initWebSocket() {
    if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) {
        return; // avoid duplicate connections
    }
    ws = new WebSocket("ws://localhost:8080");
    ws.onopen = () => {
        console.log("✅ WebSocket connected");
    };
    ws.onmessage = (event) => {
        // console.log("📨 Received from server:", event.data);
        const data = JSON.parse(event.data);
        // console.log("📨 Parsed data:", data);
        handle_received_data(data.value_dto);
    };
    ws.onclose = () => console.log("❌ WebSocket closed");
    ws.onerror = (err) => console.error("WebSocket error:", err);
}

let orders_placed = false;
let completed_orders = 0;
function handle_received_data(data) {
    // console.log("Handling received data:", data);
    if (data.type === "KitchenType" && data.attribute_name === COMPLETED_ORDERS) {
        completed_orders = data.value;
        console.log("✅ Completed orders:", completed_orders);
        if (completed_orders === order_count) {
            console.log("🎉 All orders completed!");
            if (ws) {
                ws.close();
            }
            process.exit(0);
        }
    }
    if (data.type === ALL_AGENTS_DISCOVERED_TYPE && data.value === true) {
        console.log("✅ All agents discovered, ready to place orders.");
        if (!orders_placed) {
            orders_placed = true;
            placeRandomOrder();
        }
    }
}

function placeRandomOrder() {
    if (Number.isInteger(order_count) && order_count > 0) {
        const dto = {
            context: PLACE_RANDOM_ORDER,
            order_count: order_count
        };
        console.log("Placing random order with count:", order_count);
        if (!ws) {
            console.warn("WebSocket not initialized.");
            return;
        }

        if (ws.readyState !== WebSocket.OPEN) {
            ws.addEventListener("open", () => {
                ws.send(JSON.stringify(dto));
            });
            return;
        }

        ws.send(JSON.stringify(dto));
    } else {
        console.error('Please enter a positive integer.');
        if (ws) {
            ws.close();
        }
        process.exit(1);
    }
}

initWebSocket();

program.option("-o, --order-count <number>", "Number of orders to place");
program.parse(process.argv);
const options = program.opts();
const order_count = Number(options.orderCount);


