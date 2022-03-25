
const { Splitpanes, Pane } = splitpanes
const dse = {
    id: "dse",
    label: "Dead statement elimination"
}
const zcs = {
    id: "zcs",
    label: "Z3 condition simplification"
}
const ncp = {
    id: "ncp",
    label: "Nested condition propagation"
}
const nsc = {
    id: "nsc",
    label: "Nested scope combination"
}
const cbr = {
    id: "cbr",
    label: "Condition-based refinement"
}
const rbr = {
    id: "rbr",
    label: "Reach-based refinement"
}
const lr = {
    id: "lr",
    label: "Loop refinement"
}
const ec = {
    id: "ec",
    label: "Expression combination"
}
const nc = {
    id: "nc",
    label: "Condition normalization"
}

Vue.component('list-comp', {
    props: ["items", "availableCommands", "showDelete", "title"],
    methods: {
        addCommand() {
            this.items.push(this.selected)
        },
        deleteCommand(i) {
            this.items.splice(i, 1)
        },
        addFixpoint() {
            this.items.push([])
        }
    },
    data() {
        return {
            selected: null
        }
    },
    template: "#list-component"
})

Vue.component('tree-view', {
    props: ["entries", "name"],
    methods: {
        addCommand() {
            this.items.push(this.selected)
        },
        deleteCommand(i) {
            this.items.splice(i, 1)
        },
        addFixpoint() {
            this.items.push([])
        }
    },
    data() {
        return {
            selected: null
        }
    },
    template: "#tree-view"
})

const app = new Vue({
    el: '#app',
    components: { Splitpanes, Pane },
    data: {
        dragging: false,
        ast: null,
        module: null,
        status: "Ready.",
        file: null,
        running: false,
        angha: [],
        available_commands: [
            dse,
            zcs,
            ncp,
            nsc,
            cbr,
            rbr,
            lr,
            ec,
            nc
        ],
        actions: [
            {
                url: "/action/remove-phi-nodes",
                desc: "Remove PHI nodes",
            },
            {
                url: "/action/lower-switches",
                desc: "Lower switches",
            },
            {
                url: "/action/remove-array-arguments",
                desc: "Remove array arguments",
            },
            {
                url: "/action/remove-insertvalue",
                desc: "Remove insertvalue",
            }
        ],
        commands: [],
        provenance: {}
    },
    computed: {
        astView: function () {
            if (this.ast) {
                return this.ast
            } else if (this.module) {
                return "Run 'Decompile' to view AST"
            } else {
                return "No module loaded"
            }
        },
        moduleView: function () {
            if (this.module) {
                return this.module
            } else {
                return "No module loaded"
            }
        }
    },
    mounted: function () {
        (async () => {
            await Promise.all([
                this.loadModule(),
                this.loadAST(),
                this.loadProvenance(),
                this.loadAngha()])
        })()
    },
    updated: function () {
        const spans = document.querySelectorAll('.clang[id],.llvm[id]')
        for (let span of spans) {
            span.addEventListener('mouseover', e => {
                e.stopImmediatePropagation()
                span.classList.add('hover')
                for (let prov of this.provenance[span.id] ?? []) {
                    const provenanceSpan = document.getElementById(prov)
                    provenanceSpan?.classList?.add('hover')
                }
            })
            span.addEventListener('mouseleave', e => {
                span.classList.remove('hover')
                for (let prov of this.provenance[span.id] ?? []) {
                    const provenanceSpan = document.getElementById(prov)
                    provenanceSpan?.classList?.remove('hover')
                }
            })
        }
    },
    methods: {
        async loadModule() {
            res = await fetch("/action/module", {
                credentials: "include",
                method: "GET"
            })
            if (res.status != 200) {
                throw (await res.json()).message
            }
            let text = await res.text()
            this.module = text
        },
        async loadAST() {
            res = await fetch("/action/ast", {
                credentials: "include",
                method: "GET"
            })
            if (res.status != 200) {
                throw (await res.json()).message
            }
            let text = await res.text()
            this.ast = text
        },
        async loadProvenance() {
            res = await fetch("/action/provenance", {
                credentials: "include",
                method: "GET"
            })
            if (res.status != 200) {
                throw (await res.json()).message
            }
            const prov = await res.json()
            for(let map in prov) {
                for(let [from, to] of prov[map]) {
                    if(!from || !to) {
                        continue
                    }

                    const from_hex = from.toString(16)
                    const to_hex = to.toString(16)
                    if(!this.provenance[from_hex]) {
                        this.provenance[from_hex] = []
                    }
                    this.provenance[from_hex].push(to_hex)

                    if(!this.provenance[to_hex]) {
                        this.provenance[to_hex] = []
                    }
                    this.provenance[to_hex].push(from_hex)
                }
            }
        },
        async loadAngha() {
            res = await fetch("/action/angha", {
                credentials: "include",
                method: "GET"
            })
            if (res.status != 200) {
                throw (await res.json()).message
            }
            let json = await res.json()
            this.angha = json
        },
        selectFile(event) {
            this.file = event.target.files[0]
        },
        upload() {
            this.status = "Uploading...";
            (async () => {
                try {
                    let res = await fetch("/action/module", {
                        credentials: "include",
                        body: this.file,
                        method: "POST"
                    })
                    if (res.status != 200) {
                        throw (await res.json()).message
                    }
                    this.status = "Loading module text..."
                    await this.loadModule()
                    this.ast = null
                    this.status = "Ready."
                } catch (e) {
                    this.status = e
                    this.module = null
                    this.ast = null
                }
            })()
        },
        decompile() {
            this.status = "Decompiling...";
            (async () => {
                try {
                    let res = await fetch("/action/decompile", {
                        credentials: "include",
                        method: "POST"
                    })
                    if (res.status != 200) {
                        throw (await res.json()).message
                    }
                    this.status = "Loading AST..."
                    await this.loadAST()
                    this.status = "Loading provenance info..."
                    this.provenance = {}
                    await this.loadProvenance()
                    this.status = "Ready."
                } catch (e) {
                    this.status = e
                    this.ast = null
                }
            })()
        },
        runAction(action) {
            this.status = `${action.desc}...`;
            (async () => {
                try {
                    let res = await fetch(action.url, {
                        credentials: "include",
                        method: "POST"
                    })
                    if (res.status != 200) {
                        throw (await res.json()).message
                    }
                    this.status = (await res.json()).message
                    this.loadModule()
                } catch (e) {
                    this.status = e
                    this.ast = null
                }
            })()
        },
        clearCommands() {
            this.commands = []
        },
        useDefaultChain() {
            this.commands = [
                dse,
                [zcs, ncp, nsc, cbr, rbr],
                [lr, nsc],
                [zcs, ncp, nsc],
                ec
            ]
        },
        openAngha() {
            this.$refs.anghaDialog.showModal()
        },
        anghaClosed(event) {
            console.log("anghaClosed", event.returnValue)
            if (event.returnValue) {
                const path = this.$refs.anghaDialog.returnValue;
                console.log("path: ", path);
                (async () => {
                    try {
                        this.status = `Loading ${path}...`
                        this.running = true
                        let res = await fetch("/action/loadAngha", {
                            credentials: "include",
                            body: JSON.stringify(path),
                            method: "POST"
                        })
                        if (res.status != 200) {
                            throw (await res.json()).message
                        }
                        await this.loadModule()
                        this.ast = null
                        this.status = "Ready."
                    } catch (e) {
                        this.status = e
                        this.module = null
                        this.ast = null
                    }
                })()
            }
        },
        run() {
            (async () => {
                try {
                    this.status = "Executing passes..."
                    this.running = true
                    let res = await fetch("/action/run", {
                        credentials: "include",
                        body: JSON.stringify(this.commands),
                        method: "POST"
                    })
                    if (res.status != 200) {
                        throw (await res.json()).message
                    }
                    this.status = (await res.json()).message
                    await this.loadAST()
                    this.provenance = {}
                    await this.loadProvenance()
                } catch (e) {
                    this.status = e
                } finally {
                    this.running = false
                }
            })()
        },
        fixpoint() {
            (async () => {
                try {
                    this.status = "Searching fixpoint..."
                    this.running = true
                    let res = await fetch("/action/fixpoint", {
                        credentials: "include",
                        body: JSON.stringify(this.commands),
                        method: "POST"
                    })
                    if (res.status != 200) {
                        throw (await res.json()).message
                    }
                    this.status = (await res.json()).message
                    await this.loadAST()
                    this.provenance = {}
                    await this.loadProvenance()
                } catch (e) {
                    this.status = e
                } finally {
                    this.running = false
                }
            })()
        },
        stop() {
            (async () => {
                try {
                    let res = await fetch("/action/stop", {
                        credentials: "include",
                        method: "POST"
                    })
                    if (res.status != 200) {
                        throw (await res.json()).message
                    }
                } catch (e) {
                    this.status = e
                }
            })()
        }
    }
})