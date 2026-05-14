// index.js (без изменений, но можно добавить логирование)
const opcda = require('./build/Release/opcda');

if (!opcda || !opcda.OPCDA) {
    console.error('Failed to load OPCDA module');
    process.exit(1);
}

console.log('OPCDA module loaded');

process.on('uncaughtException', (err) => {
    console.error('Uncaught exception:', err);
});
process.on('unhandledRejection', (err) => {
    console.error('Unhandled rejection:', err);
});

let client;
try {
    client = new opcda.OPCDA((event) => {
        console.log('Connection event:', event.type, event.data);
        if (event.type === 'init') {
            console.log('OPCDA initialized');
        } else if (event.type === 'connect') {
            if (event.data.success) {
                console.log('Connected to OPC server!');
                try {
                    client.createGroup('myGroup', 1000, 0.0);
                    console.log('Group created');
                    client.addItem('myGroup', '_System._ProjectTitle');
                    console.log('Item added');
                    client.subscribe('myGroup', (groupEvent) => {
                        if (groupEvent.type === 'dataChange') {
                            console.log('Data change:', groupEvent.data);
                        } else if (groupEvent.type === 'disconnect') {
                            console.error('Group disconnect:', groupEvent.data.error);
                        }
                    }, ['dataChange', 'disconnect']);
                    console.log('Subscribed to group events');
                } catch (err) {
                    console.error('Error setting up group:', err);
                }
            } else {
                console.error('Connection failed:', event.data.error);
            }
        } else if (event.type === 'disconnect') {
            console.error('Disconnected:', event.data.error);
        }
    });
    console.log('OPCDA instance created');
} catch (err) {
    console.error('Failed to create OPCDA instance:', err);
    process.exit(1);
}

console.log('Connecting to localhost with progId opcserversim.Instance.1...');
try {
    client.connect('localhost', 'opcserversim.Instance.1');
} catch (err) {
    console.error('Connect call failed:', err);
}

client.browse('').then(items => {
    console.log('Browsed items:', items);
}).catch(err => {
    console.error('Browse error:', err);
});

client.read('_System._ProjectTitle').then(value => {
    console.log('Read value:', value);
}).catch(err => {
    console.error('Read error:', err);
});

client.write('Some.Item', 42).then(() => {
    console.log('Write OK');
}).catch(err => {
    console.error('Write error:', err);
});

setTimeout(() => {
    console.log('Still waiting for async operations...');
}, 60000); // 60 секунд

process.on('SIGINT', () => {
    console.log('Shutting down...');
    try {
        client.disconnect();
        client.unsubscribeConnection();
    } catch (err) {
        console.error('Error during disconnect:', err);
    }
    process.exit(0);
});

console.log('OPC DA client started');