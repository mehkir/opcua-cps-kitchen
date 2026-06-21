const PLACE_RANDOM_ORDER = "PlaceRandomOrder";

function initWebSocket() {
    if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) {
        return; // avoid duplicate connections
    }
    ws = new WebSocket("ws://localhost:8080");
    ws.onopen = () => {
        console.log("✅ WebSocket connected");
    };
    ws.onmessage = (event) => {
        console.log("📨 Received from server:", event.data);
        const data = JSON.parse(event.data);
        console.log("📨 Parsed data:", data);
    };
    ws.onclose = () => console.log("❌ WebSocket closed");
    ws.onerror = (err) => console.error("WebSocket error:", err);
}

function placeRandomOrder(order_count) {
    if (Number.isInteger(order_count) && order_count > 0) {
        const dto = {
            context: PLACE_RANDOM_ORDER,
            order_count: order_count
        };
        console.log("Placing random order with count:", order_count);
        if (!ws || ws.readyState !== WebSocket.OPEN) {
            console.warn("WebSocket not open. Cannot send order.");
        } else {
            ws.send(JSON.stringify(dto), () => {
                ws.close();
            });
        }
    } else {
        console.error('Please enter a positive integer.');
    }
}

initWebSocket();

program.option("-oc, --order-count <number>", "Number of orders to place");
program.parse(process.argv);
const options = program.opts();
const order_count = Number(options.orderCount);
placeRandomOrder(order_count);


