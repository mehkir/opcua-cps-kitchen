# OPC UA CPS-Kitchen

## Intro
Cyberphysical systems in dynamic environments require adaptability to react appropriately.
The OPC UA standard facilitates service accessibility and monitoring of cyberphysical devices of the industrial domain. This implementation integrates a cyberphysical kitchen into a OPC UA structure.

## Kitchen Environment
The kitchen environment consists of several robots positioned along a circular conveyor belt that moves in one direction.
The cyberphysical kitchen employs a multiagent system to process placed orders.
In total, there are four agents: Kitchen, Robot, Conveyor and Controller.
- The Kitchen-Agent assigns placed orders to Robot-Agents and monitors the connectivity of all other agents - the Robot, Conveyor and Controller.
- Robot-Agents prepare the dishes assigned to them according to the FIFO principle. Each of them uses a certain set of tools to accomplish partial steps of an order. In other words, their capabilities differ from each other which requires them to cooperate to complete dishes.
- The Conveyor-Agent retrieves completed and partially finished dishes from the Robot-Agents and coordinates the assignment of partially finished dishes or delivers completed dishes to the Kitchen-Agent.
- The Controller-Agent monitors the operation status of Robot-Agents to choose a suitable robot for the next steps of a dish, when the Kitchen- or Conveyor-Agent asks for it.

## Kitchen Workflow
The sequence diagram below shows the workflow of the kitchen.
First, the Kitchen-Agent asks the Controller-Agent for a suitable Robot-Agent to prepare a dish according to the placed order.
After the Controller-Agent responds with a suitable Robot-Agent, the Kitchen-Agent assigns the Robot-Agent to process the order.
The Robot-Agent prepares the dish to the best of its capabilities and notifies the Conveyor-Agent about it whereupon the Conveyor-Agent retrieves it.
The Conveyor-Agent checks then if the dish is done completely or partially.
If partially, the Conveyor-Agent asks the Controller-Agent for a suitable Robot-Agent for the next preparation steps and delivers it there.
Otherwise the Conveyor-Agent delivers a completed dish to the Kitchen-Agent.

![Sequence Diagram](figures/cps_kitchen_sequence_diagram.svg "OPC UA Kitchen Sequence Diagram")

## Robot Capabilities
```json
{
    "capabilities" : ["boil","braise","fry"]
}
```

## Recipes
```json
"1" : {
    "name" : "pumpkin soup",
    "instructions" : [
        {
            "action" : "...",
            "ingredients" : "..."
        },
        {
            "action" : "cut",
            "ingredients" : "onion"
        },
        {
            "action" : "braise",
            "ingredients" : "pumpkin, carrot, ginger, onion"
        },
        {
            "action" : "boil",
            "ingredients" : "pumpkin, carrot, ginger",
            "duration" : 10
        },
        {
            "action" : "...",
            "ingredients" : "..."
        },
    ]
}
```


## Future tasks:
- [ ] Rearranging Robot-Agents
- [ ] Reconfiguring Robot-Agents
- [ ] MAPE-K interface