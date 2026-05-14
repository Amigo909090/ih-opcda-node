const opcda = require('./build/Release/opcda');
const client = new opcda.OPCDA((event) => {
    console.log('Event received:', event.type);
});
client.connect('localhost', 'opcserversim.Instance.1');
setTimeout(() => console.log('Timeout'), 5000);