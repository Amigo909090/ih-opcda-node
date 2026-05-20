# opcda-node

A Node.js native addon (N-API) for **OPC DA (Data Access) 2.0** communication on **Windows x64**.  
It allows reading, writing, and subscribing to OPC items from any OPC DA 2.0 server.

This addon is built with `node-gyp`, provides prebuilt binaries for Windows x64, and requires **OPC Core Components** (or the target OPC server's runtime) to be installed.

---

## ✨ Features

- **OPC DA 2.0 protocol** support (IOPCServer, IOPCItemMgt, IOPCSyncIO, IOPCAsyncIO2, IOPCDataCallback)
- **Synchronous group & item management** – `createGroup`, `addItem`, `subscribe`, `unsubscribe`
- **Asynchronous read/write** – `read` and `write` return `Promise`, executed in background threads
- **Real‑time data change notifications** – subscribe to OPC groups, receive `dataChange` events
- **Browse server address space** – list all available item IDs
- **Easy‑to‑use event‑driven API** – single callback for all events (`connect`, `disconnect`, `dataChange`)
- **Prebuilt binaries for Windows x64** – no compilation required on target machines (if OPC Core Components are present)

---

## 🖥️ Supported Platform

| Operating System | Architecture | Prebuilt binary |
|-----------------|--------------|-----------------|
| Windows         | x64          | ✅ yes          |

> **Note:** Only Windows x64 is supported. The addon uses COM/DCOM and OPC DA 2.0 interfaces, which are available only on Windows.

---

## 🚀 Installation

### Prerequisites

1. **Node.js** 18 or higher (tested with Node 20).
2. **OPC Core Components** (or the runtime provided by your OPC server).  
   You can download the official redistributable from the [OPC Foundation](https://opcfoundation.org/developer-tools/developer-kits/classic/core-components) or install it together with your OPC server (e.g. OPC Simulator).

   > If OPC Core Components are missing, the addon may fail to create COM objects.

3. **No C++ compiler required** on the target machine – prebuilt binary is included.

# @amigo9090/opcda-node

Install from npm:

```bash
npm install @amigo9090/opcda-node
```

## 📖 Usage
### Basic example

```javascript
const opcda = require('@amigo9090/opcda-node');

const client = new opcda.OPCDA((event) => {
    console.log('Event:', event.type, event.data);

    if (event.type === 'connect') {
        if (event.data.success) {
            console.log('✅ Connected to OPC server!');
            // Create group, add items, subscribe...
            client.createGroup('Group1', 1000, 0.0);
            client.addItem('Group1', 'Random.Int1');
            client.subscribe('Group1');   // subscribe to data changes
        } else {
            console.error('Connection failed:', event.data.error);
        }
    } else if (event.type === 'dataChange') {
        console.log('Data change:', event.data);
    } else if (event.type === 'disconnect') {
        console.log('Disconnected');
    }
});

client.connect('localhost', 'OPC.Simulator.1');

```
## API methods
Method	Description
connect(host, progId)	Synchronous – connects to an OPC DA server (e.g. OPC.Simulator.1). Result is reported via the connect event.
disconnect()	Synchronous – closes the connection.
createGroup(groupName, updateRate, deadband)	Synchronous – creates a group.
addItem(groupName, itemName)	Synchronous – adds an item to a group.
subscribe(groupName)	Synchronous – enables async data change notifications for the group.
unsubscribe(groupName)	Synchronous – disables async notifications.
read(itemName)	Asynchronous – returns a Promise that resolves with the current value.
write(itemName, value)	Asynchronous – returns a Promise that resolves when the write is complete.
browse(startingItem)	Asynchronous – returns a Promise that resolves with an array of all item IDs.

## Events
The constructor callback receives an event object with a type field:

connect – event.data.success (boolean), event.data.error (if failed)

disconnect – event.data is an empty object

dataChange – event.data is an object where keys are item names and values are their current values.

## 🔧 Building from source (optional)
If you need to rebuild the addon (e.g., for debugging or custom modifications):

```bash
git clone https://github.com/Amigo909090/ih-opcda-node.git
cd ih-opcda-node
npm install
npm run build
```

### Requirements:

- Windows 10/11, Visual Studio 2022 (with C++ development tools and ATL/MFC)

- Python 3.11

- Node.js 18+

- OPC Core Components (already installed)

## 📚 Additional Examples
See the examples/ folder for more complete usage scenarios.

## 🤝 Contributing
Contributions are welcome!

1. Fork the repository.

2. Create a feature branch.

3. Submit a pull request with a clear description of your changes.

Please open an issue first to discuss major changes.

## 📜 License
This project is licensed under the MIT License.

## 💬 Support
For bugs, questions, or feature requests, please use the [GitHub Issues page] (https://github.com/Amigo909090/ih-opcda-node/issues).